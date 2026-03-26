#include "threadpool.h"

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stddef.h>

struct task {
  void (*function)(void *);
  void *args;
};

struct worker {
  pthread_t thread;

  struct task *queue;
  struct threadpool *threadpool;

  int head;
  int tail;
  int count;

  pthread_mutex_t queue_lock;
  pthread_cond_t queue_not_empty;
  pthread_cond_t queue_not_full;
};

struct threadpool {
  size_t num_workers;
  struct worker *workers;

  int queue_size;

  int current_worker;
  int shutdown;
  int total_tasks;

  pthread_mutex_t threadpool_lock;
  pthread_cond_t all_done;
};

static void *worker_func(void *arg);

struct threadpool *threadpool_create(size_t num_workers, size_t queue_size) {
  struct threadpool *pool;
  if ((pool = malloc(sizeof(*pool))) == NULL) {
    fprintf(stderr, "Error in allocating memory to threadpool\n");
    return NULL;
  }
  
  if((pool->workers = malloc(sizeof(struct worker) * num_workers)) == NULL){
    fprintf(stderr, "Error in allocating memory to threadpool workers\n");
    free(pool);
    return NULL;  
  }
  
  if(pthread_mutex_init(&pool->threadpool_lock, NULL) != 0){
    free(pool->workers);
    free(pool);
    return NULL;
  }

  if (pthread_cond_init(&pool->all_done, NULL) != 0) {
    pthread_mutex_destroy(&pool->threadpool_lock);
    free(pool->workers);
    free(pool);
    return NULL;
  }

  pool->num_workers = num_workers;
  pool->queue_size = queue_size;
  pool->shutdown = 0;
  pool->current_worker = 0;

  for (int i = 0; i < num_workers; i++) {
    struct worker *worker = &pool->workers[i];
    worker->threadpool = pool;
    if (worker_init(worker, queue_size) != 0) {
      fprintf(stderr, "Failed in worker_init\n");
      for (int j = 0; j < i; j++) {
        free(pool->workers[j].queue);
        pthread_mutex_destroy(&pool->workers[j].queue_lock);
        pthread_cond_destroy(&pool->workers[j].queue_not_empty);
        pthread_cond_destroy(&pool->workers[j].queue_not_full);
      }
      free(pool->workers);
      pthread_mutex_destroy(&pool->threadpool_lock);
      pthread_cond_destroy(&pool->all_done);
      free(pool);
      return NULL;
    }
  }

  for (size_t i = 0; i < num_workers; i++) {
    if (pthread_create(&pool->workers[i].thread, NULL, worker_func, &pool->workers[i]) != 0) {
      fprintf(stderr, "Error in creating worker threads\n");

      for (int j = 0; j <= i; j++) {
        pthread_mutex_lock(&pool->workers[j].queue_lock);
        pool->shutdown = 1;
        pthread_cond_broadcast(&pool->workers[j].queue_not_empty);
        pthread_mutex_unlock(&pool->workers[j].queue_lock);
      }

      for (size_t j = 0; j < i; j++) {
        pthread_join(pool->workers[j].thread, NULL);
        pthread_mutex_destroy(&pool->workers[j].queue_lock);
        pthread_cond_destroy(&pool->workers[j].queue_not_empty);
        pthread_cond_destroy(&pool->workers[j].queue_not_full);
        free(pool->workers[j].queue);
      }

      pthread_mutex_destroy(&pool->threadpool_lock);
      pthread_cond_destroy(&pool->all_done);
      free(pool->workers);
      free(pool);
      return NULL;
    }
  }
  return pool;
}

int worker_init(struct worker *worker, int queue_size) {
  worker->head = 0;
  worker->tail = 0;
  worker->count = 0;

  if ((worker->queue = malloc(sizeof(struct task) * queue_size)) == NULL) {
    return -1;
  }
  if (pthread_mutex_init(&worker->queue_lock, NULL) != 0) {
    free(worker->queue);
    return -1;
  }
  if (pthread_cond_init(&worker->queue_not_empty, NULL) != 0) {
    free(worker->queue);
    pthread_mutex_destroy(&worker->queue_lock);
    return -1;
  }
  if (pthread_cond_init(&worker->queue_not_full, NULL) != 0) {
    free(worker->queue);
    pthread_mutex_destroy(&worker->queue_lock);
    pthread_cond_destroy(&worker->queue_not_empty);
    return -1;
  }
  return 0;
}

int threadpool_submit(struct threadpool *pool, void (*function)(void *), void *arg) {
  pthread_mutex_lock(&pool->threadpool_lock);

  if (pool->shutdown == 1) {
    pthread_mutex_unlock(&pool->threadpool_lock);
    return THREADPOOL_SHUTDOWN;
  }
  int worker_index = pool->current_worker;
  pool->current_worker = (pool->current_worker + 1) % pool->num_workers;

  pthread_mutex_unlock(&pool->threadpool_lock);

  struct worker *worker = &pool->workers[worker_index];

  pthread_mutex_lock(&worker->queue_lock);
  while (worker->count == pool->queue_size && !pool->shutdown) {
    pthread_cond_wait(&worker->queue_not_full, &worker->queue_lock);
  }
  if (pool->shutdown) {
    pthread_mutex_unlock(&worker->queue_lock);
    return THREADPOOL_SHUTDOWN;
  }

  struct task task = {.function = function, .args = arg};
  worker->queue[worker->tail] = task;
  worker->tail = (worker->tail + 1) % pool->queue_size;
  worker->count++;
  pool->total_tasks++;

  pthread_cond_signal(&worker->queue_not_empty);
  pthread_mutex_unlock(&worker->queue_lock);

  return THREADPOOL_SUBMIT_SUCCESS;
}

int threadpool_try_submit(struct threadpool *pool, void (*function)(void *), void *arg) {
  pthread_mutex_lock(&pool->threadpool_lock);

  if (pool->shutdown == 1) {
    pthread_mutex_unlock(&pool->threadpool_lock);
    return THREADPOOL_SHUTDOWN;
  }

  int worker_index = pool->current_worker;
  struct worker *worker = &pool->workers[worker_index];

  pthread_mutex_lock(&worker->queue_lock);
  if (worker->count == pool->queue_size) {
    pthread_mutex_unlock(&worker->queue_lock);
    return THREADPOOL_QUEUE_FULL;
  }

  struct task task = {.function = function, .args = arg};
  worker->queue[worker->tail] = task;
  worker->tail = (worker->tail + 1) % pool->num_workers;
  worker->count++;

  
  pool->current_worker = (pool->current_worker + 1) % pool->queue_size;

  pthread_cond_signal(&worker->queue_not_empty);
  pthread_mutex_unlock(&worker->queue_lock);
  pthread_mutex_unlock(&pool->threadpool_lock);

  return THREADPOOL_SUBMIT_SUCCESS;
}

static void *worker_func(void *arg) {
  struct worker *worker = arg;
  struct threadpool *pool = worker->threadpool;
  while (1) {
    struct task task;
    pthread_mutex_lock(&worker->queue_lock);
    while (worker->count == 0 && !pool->shutdown) {
      pthread_cond_wait(&worker->queue_not_empty, &worker->queue_lock);
    }
    if (pool->shutdown && worker->count == 0) {
      pthread_mutex_unlock(&worker->queue_lock);
      break;
    }

    task = worker->queue[worker->head];
    worker->head = (worker->head + 1) % pool->queue_size;
    worker->count--;

    pthread_cond_signal(&worker->queue_not_full);
    pthread_mutex_unlock(&worker->queue_lock);

    task.function(task.args);

    pthread_mutex_lock(&pool->threadpool_lock);
    pool->total_tasks--;
    if (pool->total_tasks == 0) {
      pthread_cond_broadcast(&pool->all_done);
    }
    pthread_mutex_unlock(&pool->threadpool_lock);
  }
  return NULL;
}

void threadpool_wait(struct threadpool *pool) {
  pthread_mutex_lock(&pool->threadpool_lock);
  while (pool->total_tasks > 0) {
    pthread_cond_wait(&pool->all_done, &pool->threadpool_lock);
  }
  pthread_mutex_unlock(&pool->threadpool_lock);
}

void threadpool_destroy(struct threadpool *pool) {
  pthread_mutex_lock(&pool->threadpool_lock);
  pool->shutdown = 1;
  for (int i = 0; i < pool->num_workers; i++) {
    pthread_mutex_lock(&pool->workers[i].queue_lock);
    pthread_cond_broadcast(&pool->workers[i].queue_not_empty);
    pthread_cond_broadcast(&pool->workers[i].queue_not_full);
    pthread_mutex_unlock(&pool->workers[i].queue_lock);
  }
  for (size_t i = 0; i < pool->num_workers; i++) {
    pthread_join(pool->workers[i].thread, NULL);
  }
  for(int i = 0; i < pool->num_workers; i++){
    pthread_mutex_destroy(&pool->workers[i].queue_lock);
    pthread_cond_destroy(&pool->workers[i].queue_not_empty);
    pthread_cond_destroy(&pool->workers[i].queue_not_full);
    free(pool->workers[i].queue);
  }
  pthread_cond_destroy(&pool->all_done);
  free(pool->workers);
  free(pool);
}
