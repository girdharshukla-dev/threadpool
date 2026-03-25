#include "../threadpool.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void example_task(void *arg) {
  int id = *(int *)arg;
  printf("Task %d started on thread\n", id);
  usleep(1000000);
  printf("Task %d finished\n", id);
  free(arg);
}

int main() {
  int num_threads = 4;
  int queue_size = 8;

  struct threadpool *pool = threadpool_create(num_threads, queue_size);
  if (!pool) {
    fprintf(stderr, "Failed to create threadpool\n");
    return 1;
  }

  printf("Submitting tasks...\n");

  for (int i = 0; i < 10; i++) {
    int *arg = malloc(sizeof(int));
    *arg = i;

    if (threadpool_submit(pool, example_task, arg) != 0) {
      fprintf(stderr, "Failed to submit task %d\n", i);
      free(arg);
    }
  }

  printf("Waiting for tasks to complete...\n");
  threadpool_wait(pool);
  printf("First batch completed\n");

  printf("Submitting second batch...\n");

  for (int i = 10; i < 15; i++) {
    int *arg = malloc(sizeof(int));
    *arg = i;
    int res = threadpool_submit(pool, example_task, arg);
    if (res == THREADPOOL_QUEUE_FULL) {
      printf("Queue full for task %d\n", i);
      free(arg);
    }
  }

  threadpool_wait(pool);
  printf("All tasks done\n");
  threadpool_destroy(pool);
  printf("Threadpool destroyed\n");

  return 0;
}
