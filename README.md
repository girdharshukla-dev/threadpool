# Threadpool

A small thread pool implementation in C built using pthreads. (will not work on windows)  
It uses a bounded circular queue and a fixed number of worker threads.

## Build

```bash
gcc -pthread threadpool.c examples/example.c -o example
```
Refer to examples/example.c for usage example

NOTE: There is a also an implementation of per-worker queues under `per-worker-q` branch, itworks for medium workloads but failing correctness under heavy workload along with the explosion in complexity of the program.

NOTE: I am doing this as a creative side project to burn time.
