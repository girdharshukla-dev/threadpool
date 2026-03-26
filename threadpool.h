#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <stddef.h>

#define THREADPOOL_SUBMIT_SUCCESS 0
#define THREADPOOL_SHUTDOWN -1
#define THREADPOOL_QUEUE_FULL -2

struct threadpool;

struct threadpool *threadpool_create(size_t num_workers, size_t queue_size);
int threadpool_submit(struct threadpool *pool, void (*function)(void*), void *arg);
int threadpool_try_submit(struct threadpool *pool, void (*function)(void*), void *arg);
void threadpool_wait(struct threadpool *pool);
void threadpool_destroy(struct threadpool *pool);

#endif