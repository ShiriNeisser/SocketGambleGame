#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "server.h"

#define THREAD_POOL_MIN_WORKERS 4  /* floor if core-count detection fails */
#define JOB_QUEUE_CAPACITY 2048

typedef enum {
    JOB_PROCESS_MESSAGE,
    JOB_SEND_FINAL,
    JOB_DISCONNECT
} JobType;

typedef struct {
    JobType  type;
    Client  *client;
    char    *message;       /* owned by job; freed by worker */
    int      wrong_message; /* for JOB_SEND_FINAL */
    int      epoll_fd;      /* for JOB_DISCONNECT */
} Job;

typedef struct {
    Job             queue[JOB_QUEUE_CAPACITY];
    int             head;
    int             tail;
    int             count;
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
    pthread_t      *workers;    /* heap-allocated, sized at pool_init() time */
    int             num_workers;
    int             shutdown;
    int             started;
} ThreadPool;

extern ThreadPool g_pool;

int  pool_init(void);
int  pool_submit(const Job *job);
void pool_shutdown(void);

#endif /* THREAD_POOL_H */
