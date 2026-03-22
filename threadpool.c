#include "threadpool.h"

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

struct task {
  void (*function)(void *);
  void *args;
};

struct threadpool {
  int num_threads;
  pthread_t *threads;

  struct task *queue;
  int queue_size;
  int head;
  int tail;
  int count;
  int shutdown;

  pthread_mutex_t queue_lock;
  pthread_cond_t queue_not_empty;
  pthread_cond_t queue_not_full;
};

static void *worker(void *arg);

struct threadpool *threadpool_create(size_t num_threads, size_t queue_size) {
  struct threadpool *pool;
  if ((pool = malloc(sizeof(*pool))) == NULL) {
    fprintf(stderr, "Error in allocating memory to threadpool\n");
    return NULL;
  }
  if ((pool->threads = malloc(sizeof(pthread_t) * num_threads)) == NULL) {
    fprintf(stderr, "Error in allocating memory to threadpool threads\n");
    free(pool);
    return NULL;
  }
  if ((pool->queue = malloc(sizeof(struct task) * queue_size)) == NULL) {
    fprintf(stderr, "Error in allocating memory to threadpool threads\n");
    free(pool->threads);
    free(pool);
    return NULL;
  }
  pool->num_threads = num_threads;
  pool->queue_size = queue_size;
  pool->head = 0;
  pool->tail = 0;
  pool->count = 0;
  pool->shutdown = 0;
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

  for (size_t i = 0; i < num_threads; i++) {
    if (pthread_create(&pool->threads[i], NULL, worker, pool) != 0) {
      fprintf(stderr, "Error in creating threads\n");
      pool->shutdown = 1;

      pthread_cond_broadcast(&pool->queue_not_empty);
      
      for(size_t j = 0; j < i; j++){
        pthread_join(pool->threads[j], NULL);
      }

      pthread_mutex_destroy(&pool->queue_lock);
      pthread_cond_destroy(&pool->queue_not_empty);
      pthread_cond_destroy(&pool->queue_not_full);

      free(pool->threads);
      free(pool->queue);
      free(pool);
      return NULL;
    }
  }
  return pool;
}

void threadpool_submit(struct threadpool *pool, void (*function)(void *),
                       void *arg) {
  pthread_mutex_lock(&pool->queue_lock);
  while (pool->count == pool->queue_size) {
    fprintf(stderr, "Threadpool queue full\n");
    pthread_cond_wait(&pool->queue_not_full, &pool->queue_lock);
  }
  if (pool->shutdown) {
    pthread_mutex_unlock(&pool->queue_lock);
    return;
  }
  struct task task;
  task.args = arg;
  task.function = function;
  pool->queue[pool->tail] = task;
  pool->tail = (pool->tail + 1) % pool->queue_size;
  pool->count++;
  pthread_cond_signal(&pool->queue_not_empty);
  pthread_mutex_unlock(&pool->queue_lock);
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
    pthread_cond_signal(&pool->queue_not_full);
    pthread_mutex_unlock(&pool->queue_lock);
    task.function(task.args);
  }
  return NULL;
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
  free(pool->threads);
  free(pool->queue);
  free(pool);
}
