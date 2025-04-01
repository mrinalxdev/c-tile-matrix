#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <stddef.h>

typedef struct {
    void (*function)(void *);
    void *argument;
} Task;

typedef struct {
    Task *tasks;
    size_t capacity;
    size_t head;
    size_t tail;
    pthread_mutex_t lock;
} TaskQueue;

typedef struct {
    TaskQueue *queues;     // One queue per worker
    pthread_t *threads;    // Worker threads
    int num_workers;
    int shutdown;
    pthread_cond_t task_cond;  // Condition variable for task notification
} ThreadPool;

ThreadPool *threadpool_create(int num_workers);
void threadpool_add_task(ThreadPool *pool, void (*function)(void *), void *arg);
void threadpool_destroy(ThreadPool *pool);  // Fixed typo here

#endif // THREADPOOL_H
