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

  int shutdown;
  int tasks_in_progress;

  pthread_cond_t all_done;
};

static void *worker(void *arg);

struct threadpool *threadpool_create(size_t num_workers, size_t queue_size) {
  struct threadpool *pool;
  if ((pool = malloc(sizeof(*pool))) == NULL) {
    fprintf(stderr, "Error in allocating memory to threadpool\n");
    return NULL;
  }

  if((pool->workers = malloc(sizeof(struct worker) * num_workers)) == NULL){
    fprintf(stderr, "Error in allocating memory to workers\n");
    free(pool);
    return NULL;
  }

  pool->num_workers = num_workers;
  pool->queue_size = queue_size;
  pool->tasks_in_progress = 0;
  pool->shutdown = 0;

  for(size_t i = 0; i < num_workers; i++){
    struct worker *worker = &pool->workers[i];
    worker->head = 0;
    worker->tail = 0;
    worker->count = 0;
    if((worker->queue = malloc(queue_size * sizeof(struct task))) == NULL){
      
    }
  }

  if (pthread_mutex_init(&pool->queue_lock, NULL) != 0) {
    fprintf(stderr, "Error in pthread_mutex_init of threadpool\n");
    free(pool->threads);
    free(pool->queue);
    free(pool);
    return NULL;
  }
  if (pthread_cond_init(&pool->queue_not_full, NULL) != 0) {
    fprintf(stderr, "Error in pthread_cond_init of queue_not_full\n");
    free(pool->threads);
    free(pool->queue);
    pthread_mutex_destroy(&pool->queue_lock);
    free(pool);
    return NULL;
  }
  if (pthread_cond_init(&pool->queue_not_empty, NULL) != 0) {
    fprintf(stderr, "Error in pthread_cond_init of queue_not_empty\n");
    free(pool->threads);
    free(pool->queue);
    pthread_mutex_destroy(&pool->queue_lock);
    pthread_cond_destroy(&pool->queue_not_full);
    free(pool);
    return NULL;
  }
  if(pthread_cond_init(&pool->all_done, NULL) != 0){
    fprintf(stderr, "Error in pthread_cond_init of all_done\n");
    free(pool->threads);
    free(pool->queue);
    pthread_mutex_destroy(&pool->queue_lock);
    pthread_cond_destroy(&pool->queue_not_full);
    pthread_cond_destroy(&pool->queue_not_empty);
    free(pool);
    return NULL;
  }

  for (size_t i = 0; i < num_threads; i++) {
    if (pthread_create(&pool->threads[i], NULL, worker, pool) != 0) {
      fprintf(stderr, "Error in creating threads\n");
      pool->shutdown = 1;

      pthread_cond_broadcast(&pool->queue_not_empty);

      for (size_t j = 0; j < i; j++) {
        pthread_join(pool->threads[j], NULL);
      }

      pthread_mutex_destroy(&pool->queue_lock);
      pthread_cond_destroy(&pool->queue_not_empty);
      pthread_cond_destroy(&pool->queue_not_full);
      pthread_cond_destroy(&pool->all_done);

      free(pool->threads);
      free(pool->queue);
      free(pool);
      return NULL;
    }
  }
  return pool;
}

int threadpool_submit(struct threadpool *pool, void (*function)(void *),
                      void *arg) {
  pthread_mutex_lock(&pool->queue_lock);

  while (pool->count == pool->queue_size && !pool->shutdown) {
    // fprintf(stderr, "Threadpool queue full\n");
    pthread_cond_wait(&pool->queue_not_full, &pool->queue_lock);
  }

  if (pool->shutdown) {
    pthread_mutex_unlock(&pool->queue_lock);
    return THREADPOOL_SHUTDOWN;
  }
  
  struct task task;
  task.args = arg;
  task.function = function;
  pool->queue[pool->tail] = task;
  pool->tail = (pool->tail + 1) % pool->queue_size;
  pool->count++;

  pthread_cond_signal(&pool->queue_not_empty);
  pthread_mutex_unlock(&pool->queue_lock);

  return THREADPOOL_SUBMIT_SUCCESS;
}

int threadpool_try_submit(struct threadpool *pool, void (*function)(void *),
                          void *arg) {
  pthread_mutex_lock(&pool->queue_lock);

  if (pool->shutdown) {
    pthread_mutex_unlock(&pool->queue_lock);
    return THREADPOOL_SHUTDOWN;
  }
  if (pool->count == pool->queue_size) {
    pthread_mutex_unlock(&pool->queue_lock);
    return THREADPOOL_QUEUE_FULL;
  }

  struct task task;
  task.args = arg;
  task.function = function;
  pool->queue[pool->tail] = task;
  pool->tail = (pool->tail + 1) % pool->queue_size;
  pool->count++;

  pthread_cond_signal(&pool->queue_not_empty);
  pthread_mutex_unlock(&pool->queue_lock);

  return THREADPOOL_SUBMIT_SUCCESS;
}

static void *worker(void *arg) {
  struct threadpool *pool = arg;
  while (1) {
    struct task task;
    pthread_mutex_lock(&pool->queue_lock);
    while (pool->count == 0 && !pool->shutdown) {
      pthread_cond_wait(&pool->queue_not_empty, &pool->queue_lock);
    }
    if (pool->shutdown && pool->count == 0) {
      pthread_mutex_unlock(&pool->queue_lock);
      break;
    }
    
    task = pool->queue[pool->head];
    pool->head = (pool->head + 1) % pool->queue_size;
    pool->count--;
    pool->tasks_in_progress++;
    
    pthread_cond_signal(&pool->queue_not_full);
    pthread_mutex_unlock(&pool->queue_lock);
    
    task.function(task.args);

    pthread_mutex_lock(&pool->queue_lock);
    pool->tasks_in_progress--;
    if(pool->tasks_in_progress == 0 && pool->count == 0){
      pthread_cond_broadcast(&pool->all_done);
      // there might be case where signal is a problem, like assume there are thread A and thread B
      // who called threadpool_wait, for instance the condition variable is signaled not broadcasted
      // thread A might wake up, exit the function and thread B might still sleep since it wasnt 
      // broadcasted, since thread A is now free, it may submit new tasks and when thread B checks for
      // condition all_done condition variable whenever, it may still see some tasks in progress 
      // and again go to sleep and this can continue and thread B might always go to sleep
    }
    pthread_mutex_unlock(&pool->queue_lock);
    
  }
  return NULL;
}

void threadpool_wait(struct threadpool* pool){
  pthread_mutex_lock(&pool->queue_lock);
  while(pool->count > 0 || pool->tasks_in_progress > 0){
    pthread_cond_wait(&pool->all_done, &pool->queue_lock);
  }
  pthread_mutex_unlock(&pool->queue_lock);
}

void threadpool_destroy(struct threadpool *pool) {
  pthread_mutex_lock(&pool->queue_lock);
  pool->shutdown = 1;
  pthread_cond_broadcast(&pool->queue_not_empty);
  pthread_cond_broadcast(&pool->queue_not_full);
  pthread_mutex_unlock(&pool->queue_lock);
  for (size_t i = 0; i < pool->num_threads; i++) {
    pthread_join(pool->threads[i], NULL);
  }
  pthread_mutex_destroy(&pool->queue_lock);
  pthread_cond_destroy(&pool->queue_not_empty);
  pthread_cond_destroy(&pool->queue_not_full);
  pthread_cond_destroy(&pool->all_done);
  free(pool->threads);
  free(pool->queue);
  free(pool);
}
