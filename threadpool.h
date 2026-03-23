#ifndef THREADPOOL_H
#define THREADPOOL_H

struct threadpool;

struct threadpool *threadpool_create(size_t num_threads, size_t queue_size);
int threadpool_submit(struct threadpool *pool, void (*function)(void*), void *arg);
int threadpool_try_submit(struct threadpool *pool, void (*function)(void*), void *arg);
void threadpool_wait(struct threadpool *pool, void (*function)(void*), void *arg);
void threadpool_destroy(struct threadpool *pool);

#endif