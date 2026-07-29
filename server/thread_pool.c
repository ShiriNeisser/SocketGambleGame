#include "thread_pool.h"

ThreadPool g_pool;

static void task_queue_init(TaskQueue *q) {
    q->head  = 0;
    q->tail  = 0;
    q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

static void task_queue_destroy(TaskQueue *q) {
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}

static void enqueue_locked(TaskQueue *q, Client *client) {
    q->tasks[q->tail].client = client;
    q->tail = (q->tail + 1) % QUEUE_CAPACITY;
    q->count++;
}

static Client *dequeue_locked(TaskQueue *q) {
    Client *client = q->tasks[q->head].client;
    q->head = (q->head + 1) % QUEUE_CAPACITY;
    q->count--;
    return client;
}

void *worker_loop(void *arg) {
    ThreadPool *pool = (ThreadPool *)arg;

    while (1) {
        pthread_mutex_lock(&pool->queue.mutex);

        while (pool->queue.count == 0 && !pool->shutdown)
            pthread_cond_wait(&pool->queue.not_empty, &pool->queue.mutex);

        if (pool->shutdown && pool->queue.count == 0) {
            pthread_mutex_unlock(&pool->queue.mutex);
            break;
        }

        Client *client = dequeue_locked(&pool->queue);
        pthread_cond_signal(&pool->queue.not_full);
        pthread_mutex_unlock(&pool->queue.mutex);

        /* Handle client outside the queue lock so workers run in parallel. */
        handle_new_client(client);
        if (client->bet_received && client->connected)
            handle_client_requests(client);
    }

    return NULL;
}

void thread_pool_init(ThreadPool *pool, int num_workers) {
    if (num_workers > NUM_WORKERS)
        num_workers = NUM_WORKERS;
    if (num_workers < 1)
        num_workers = 1;

    pool->num_workers = num_workers;
    pool->shutdown    = 0;
    task_queue_init(&pool->queue);

    for (int i = 0; i < num_workers; i++) {
        if (pthread_create(&pool->workers[i], NULL, worker_loop, pool) != 0) {
            perror("pthread_create worker");
            exit(EXIT_FAILURE);
        }
        printf("Worker thread %d started.\n", i);
    }
    printf("Thread pool initialized with %d workers.\n", num_workers);
}

void thread_pool_submit(ThreadPool *pool, Client *client) {
    pthread_mutex_lock(&pool->queue.mutex);

    while (pool->queue.count == QUEUE_CAPACITY && !pool->shutdown)
        pthread_cond_wait(&pool->queue.not_full, &pool->queue.mutex);

    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->queue.mutex);
        return;
    }

    enqueue_locked(&pool->queue, client);
    pthread_cond_signal(&pool->queue.not_empty);
    pthread_mutex_unlock(&pool->queue.mutex);
}

void thread_pool_shutdown(ThreadPool *pool) {
    pthread_mutex_lock(&pool->queue.mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->queue.not_empty);
    pthread_cond_broadcast(&pool->queue.not_full);
    pthread_mutex_unlock(&pool->queue.mutex);

    for (int i = 0; i < pool->num_workers; i++)
        pthread_join(pool->workers[i], NULL);

    task_queue_destroy(&pool->queue);
    printf("Thread pool shut down.\n");
}
