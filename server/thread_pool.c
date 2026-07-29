#include "thread_pool.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

ThreadPool g_pool;

static void strip_trailing_ws(char *s) {
    if (!s)
        return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) {
        s[n - 1] = '\0';
        n--;
    }
}

static void dispatch_job(Job *job) {
    switch (job->type) {
    case JOB_PROCESS_MESSAGE:
        if (!job->client || !job->message)
            break;
        strip_trailing_ws(job->message);
        switch (job->client->state) {
        case CLIENT_WAIT_AUTH:
            handle_auth_message(job->client, job->message);
            break;
        case CLIENT_WAIT_BET:
            handle_bet_message(job->client, job->message);
            break;
        case CLIENT_IN_GAME:
            handle_game_message(job->client, job->message);
            break;
        default:
            break;
        }
        break;

    case JOB_SEND_FINAL:
        if (job->client)
            send_final_message(job->client, job->wrong_message);
        break;

    case JOB_DISCONNECT:
        if (job->client) {
            if (job->epoll_fd >= 0 && job->client->socket >= 0)
                epoll_ctl(job->epoll_fd, EPOLL_CTL_DEL, job->client->socket, NULL);
            if (job->client->socket >= 0) {
                close(job->client->socket);
                job->client->socket = -1;
            }
            job->client->connected = 0;
            job->client->state = CLIENT_DISCONNECTED;
        }
        break;
    }

    free(job->message);
    job->message = NULL;
}

static void *worker_main(void *arg) {
    (void)arg;
    while (1) {
        pthread_mutex_lock(&g_pool.mutex);
        while (g_pool.count == 0 && !g_pool.shutdown)
            pthread_cond_wait(&g_pool.not_empty, &g_pool.mutex);

        if (g_pool.shutdown && g_pool.count == 0) {
            pthread_mutex_unlock(&g_pool.mutex);
            break;
        }

        Job job = g_pool.queue[g_pool.head];
        g_pool.head = (g_pool.head + 1) % JOB_QUEUE_CAPACITY;
        g_pool.count--;
        pthread_cond_signal(&g_pool.not_full);
        pthread_mutex_unlock(&g_pool.mutex);

        dispatch_job(&job);
    }
    return NULL;
}

int pool_init(void) {
    memset(&g_pool, 0, sizeof(g_pool));
    if (pthread_mutex_init(&g_pool.mutex, NULL) != 0)
        return -1;
    if (pthread_cond_init(&g_pool.not_empty, NULL) != 0)
        return -1;
    if (pthread_cond_init(&g_pool.not_full, NULL) != 0)
        return -1;

    g_pool.shutdown = 0;
    g_pool.started = 0;

    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        if (pthread_create(&g_pool.workers[i], NULL, worker_main, NULL) != 0) {
            g_pool.shutdown = 1;
            pthread_cond_broadcast(&g_pool.not_empty);
            for (int j = 0; j < i; j++)
                pthread_join(g_pool.workers[j], NULL);
            return -1;
        }
    }
    g_pool.started = 1;
    printf("Thread pool started: %d workers, queue capacity %d\n",
           THREAD_POOL_SIZE, JOB_QUEUE_CAPACITY);
    return 0;
}

int pool_submit(const Job *job) {
    if (!job || !g_pool.started)
        return -1;

    pthread_mutex_lock(&g_pool.mutex);
    while (g_pool.count == JOB_QUEUE_CAPACITY && !g_pool.shutdown)
        pthread_cond_wait(&g_pool.not_full, &g_pool.mutex);

    if (g_pool.shutdown) {
        pthread_mutex_unlock(&g_pool.mutex);
        return -1;
    }

    g_pool.queue[g_pool.tail] = *job;
    g_pool.tail = (g_pool.tail + 1) % JOB_QUEUE_CAPACITY;
    g_pool.count++;
    pthread_cond_signal(&g_pool.not_empty);
    pthread_mutex_unlock(&g_pool.mutex);
    return 0;
}

void pool_shutdown(void) {
    if (!g_pool.started)
        return;

    pthread_mutex_lock(&g_pool.mutex);
    g_pool.shutdown = 1;
    pthread_cond_broadcast(&g_pool.not_empty);
    pthread_cond_broadcast(&g_pool.not_full);
    pthread_mutex_unlock(&g_pool.mutex);

    for (int i = 0; i < THREAD_POOL_SIZE; i++)
        pthread_join(g_pool.workers[i], NULL);

    /* Drain any leftover jobs so messages are freed */
    pthread_mutex_lock(&g_pool.mutex);
    while (g_pool.count > 0) {
        Job job = g_pool.queue[g_pool.head];
        g_pool.head = (g_pool.head + 1) % JOB_QUEUE_CAPACITY;
        g_pool.count--;
        pthread_mutex_unlock(&g_pool.mutex);
        free(job.message);
        pthread_mutex_lock(&g_pool.mutex);
    }
    pthread_mutex_unlock(&g_pool.mutex);

    g_pool.started = 0;
    printf("Thread pool shut down.\n");
}
