#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "server.h"

typedef struct {
    Client *client;
} Task;

typedef struct {
    Task            tasks[QUEUE_CAPACITY];
    int             head;
    int             tail;
    int             count;
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} TaskQueue;

typedef struct {
    pthread_t   workers[NUM_WORKERS];
    int         num_workers;
    TaskQueue   queue;
    int         shutdown;
} ThreadPool;

extern ThreadPool g_pool;

void  thread_pool_init(ThreadPool *pool, int num_workers);
void  thread_pool_submit(ThreadPool *pool, Client *client);
void  thread_pool_shutdown(ThreadPool *pool);
void *worker_loop(void *arg);

#endif /* THREAD_POOL_H */
