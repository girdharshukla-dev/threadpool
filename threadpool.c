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
  pthread_t *threads;

  struct task *queue;
  int head;
  int tail;
  int count;

  pthread_mutex_t threadpool_lock;
  pthread_cond_t threadpool_cond;
};

struct threadpool *threadpool_create(size_t num_threads, size_t queue_size) {
  struct threadpool *pool;
  if ((pool = malloc(sizeof(*pool))) == NULL) {
    fprintf(stderr, "Error in allocating memory to threadpool\n");
    return NULL;
  }
  if(pool->threads = malloc(sizeof(int) * num_threads) == NULL){
    fprintf(stderr, "Error in allocating memory to threadpool threads\n");
    return NULL;
  }
  if(pool->queue = malloc(sizeof(int) * queue_size) == NULL){
    fprintf(stderr, "Error in allocating memory to threadpool threads\n");
    return NULL;
  }
  
}
