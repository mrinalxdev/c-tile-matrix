#include "threadpool.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

/**
 * define the cache line size for alignment purposes.
 * This helps prevent false sharing between threads by ensuring
 * that frequently accessed data is on different cache lines.
 */
#define CACHE_LINE_SIZE 64

/**
 * Macro to align data structures to cache line boundaries.
 * This reduces contention in multi-threaded environments.
 */
#define ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))

/**
 * Initialize a task queue with specified capacity.
 *
 * @param queue Pointer to the TaskQueue to be initialized
 * @param capacity The maximum number of tasks the queue can hold
 *
 * The queue uses a circular buffer implementation where:
 * - head points to the next task to be processed
 * - tail points to the next free slot to add a task
 * - When head == tail, the queue is empty
 */
void task_queue_init(TaskQueue *queue, size_t capacity)
{
    queue->tasks = malloc(sizeof(Task) * capacity);
    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    pthread_mutex_init(&queue->lock, NULL);
}

/**
 * Worker thread function that runs continuously to process tasks.
 *
 * @param arg Pointer to the ThreadPool this worker belongs to
 * @return NULL when the thread exits
 *
 * Each worker thread follows a work-stealing strategy:
 * 1. First checks its own queue for tasks
 * 2. If no tasks in own queue, checks if threadpool is shutting down
 * 3. If not shutting down, tries to steal tasks from other workers' queues
 * 4. If no tasks found anywhere, waits on condition variable for notification
 */
static void *worker_thread(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;
    int worker_id = -1;

    // Find our worker ID by comparing thread IDs
    for (int i = 0; i < pool->num_workers; i++)
    {
        if (pthread_equal(pool->threads[i], pthread_self()))
        {
            worker_id = i;
            break;
        }
    }

    TaskQueue *my_queue = &pool->queues[worker_id];

    while (1)
    {
        Task task;
        int found_task = 0;

        // Try to get a task from our own queue first (LIFO behavior)
        pthread_mutex_lock(&my_queue->lock);
        if (my_queue->head != my_queue->tail)
        {
            // Queue is not empty, get the next task
            task = my_queue->tasks[my_queue->head];
            my_queue->head = (my_queue->head + 1) % my_queue->capacity;
            found_task = 1;
        }
        pthread_mutex_unlock(&my_queue->lock);

        // If found a task in our queue, execute it and continue
        if (found_task)
        {
            task.function(task.argument);
            continue;
        }

        // Check if the threadpool is shutting down
        // Note: Using the first queue's lock as a proxy for pool-wide shutdown status
        pthread_mutex_lock(&pool->queues[0].lock);
        if (pool->shutdown)
        {
            pthread_mutex_unlock(&pool->queues[0].lock);
            break; // Exit the thread if shutdown flag is set
        }
        pthread_mutex_unlock(&pool->queues[0].lock);

        // Work-stealing: Try to steal tasks from other workers' queues
        for (int i = 0; i < pool->num_workers; i++)
        {
            if (i == worker_id)
                continue; // Skip our own queue

            pthread_mutex_lock(&pool->queues[i].lock);
            if (pool->queues[i].head != pool->queues[i].tail)
            {
                // Found a task in another worker's queue
                task = pool->queues[i].tasks[pool->queues[i].head];
                pool->queues[i].head = (pool->queues[i].head + 1) % pool->queues[i].capacity;
                pthread_mutex_unlock(&pool->queues[i].lock);
                found_task = 1;
                break;
            }
            pthread_mutex_unlock(&pool->queues[i].lock);
        }

        // Execute the stolen task if found
        if (found_task)
        {
            task.function(task.argument);
            continue;
        }

        // No work found anywhere, wait for notification of new tasks
        // This reduces CPU usage when idle compared to busy-waiting
        pthread_mutex_lock(&pool->queues[0].lock);
        pthread_cond_wait(&pool->task_cond, &pool->queues[0].lock);
        pthread_mutex_unlock(&pool->queues[0].lock);
    }

    return NULL;
}

/**
 * Create a new thread pool with the specified number of worker threads.
 *
 * @param num_workers Number of worker threads to create
 * @return Pointer to the newly created ThreadPool, or NULL on failure
 *
 * This function:
 * 1. Allocates memory for the pool structure and its components
 * 2. Initializes task queues for each worker
 * 3. Creates worker threads
 */
ThreadPool *threadpool_create(int num_workers)
{
    // Allocate memory for the thread pool structure
    ThreadPool *pool = malloc(sizeof(ThreadPool));

    // Allocate memory for worker-specific queues and thread handles
    pool->queues = malloc(sizeof(TaskQueue) * num_workers);
    pool->threads = malloc(sizeof(pthread_t) * num_workers);

    pool->num_workers = num_workers;
    pool->shutdown = 0; // Not shutting down initially

    // Initialize condition variable for signaling workers
    pthread_cond_init(&pool->task_cond, NULL);

    // Initialize each worker's task queue with capacity of 1024 tasks
    for (int i = 0; i < num_workers; i++)
    {
        task_queue_init(&pool->queues[i], 1024);
    }

    // Create worker threads
    for (int i = 0; i < num_workers; i++)
    {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0)
        {
            perror("Failed to create thread");
            threadpool_destroy(pool); // Clean up on error
            return NULL;
        }
    }

    return pool;
}

/**
 * Add a new task to the thread pool.
 *
 * @param pool Pointer to the ThreadPool
 * @param function Function pointer to the task to be executed
 * @param arg Argument to be passed to the function
 *
 * This function:
 * 1. Randomly selects a worker's queue to add the task to (for load balancing)
 * 2. Adds the task to the selected queue
 * 3. Signals workers that a new task is available
 */
void threadpool_add_task(ThreadPool *pool, void (*function)(void *), void *arg)
{
    // Thread-local random seed for better performance and to avoid lock contention
    static __thread unsigned seed = 0;

    // Choose a random worker's queue for load balancing
    int worker_id = rand_r(&seed) % pool->num_workers;

    // Create a new task with the provided function and argument
    Task task = {
        .function = function,
        .argument = arg};

    // Add the task to the chosen worker's queue
    pthread_mutex_lock(&pool->queues[worker_id].lock);
    size_t next_tail = (pool->queues[worker_id].tail + 1) % pool->queues[worker_id].capacity;
    pool->queues[worker_id].tasks[pool->queues[worker_id].tail] = task;
    pool->queues[worker_id].tail = next_tail;
    pthread_mutex_unlock(&pool->queues[worker_id].lock);

    // Signal that a new task is available
    // Note: Only one thread will be woken up to handle the new task
    pthread_cond_signal(&pool->task_cond);
}

/**
 * Destroy a thread pool and release all associated resources.
 *
 * @param pool Pointer to the ThreadPool to destroy
 *
 * This function:
 * 1. Sets the shutdown flag to signal all workers to exit
 * 2. Wakes up all waiting workers
 * 3. Waits for all worker threads to finish
 * 4. Cleans up all allocated resources
 */
void threadpool_destroy(ThreadPool *pool)
{
    // Set shutdown flag to signal all workers to exit
    pthread_mutex_lock(&pool->queues[0].lock);
    pool->shutdown = 1;
    pthread_mutex_unlock(&pool->queues[0].lock);

    // Wake up all waiting worker threads so they can check the shutdown flag
    pthread_cond_broadcast(&pool->task_cond);
    for (int i = 0; i < pool->num_workers; i++)
    {
        pthread_join(pool->threads[i], NULL);
        pthread_mutex_destroy(&pool->queues[i].lock);
        free(pool->queues[i].tasks);
    }

    // Clean up remaining resources
    pthread_cond_destroy(&pool->task_cond);
    free(pool->queues);
    free(pool->threads);
    free(pool);
}
