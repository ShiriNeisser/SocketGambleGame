/* game.c */
#include "server.h"
#include "thread_pool.h"
#include <stdint.h>

static const char *countries[] = {
    "Brazil", "Germany", "Argentina", "France", "Spain",
    "Italy", "England", "Netherlands", "Portugal", "Belgium"
};

void shuffle_countries(char *shuffled[], int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        char *temp = shuffled[i];
        shuffled[i] = shuffled[j];
        shuffled[j] = temp;
    }
}

void assign_teams(GameState *gs) {
    char *shuffled[NUM_COUNTRIES];
    for (int i = 0; i < (int)NUM_COUNTRIES; i++)
        shuffled[i] = strdup(countries[i]);

    shuffle_countries(shuffled, NUM_COUNTRIES);

    strncpy(gs->group1, shuffled[0], TEAM_NAME_MAX_LENGTH - 1);
    strncpy(gs->group2, shuffled[1], TEAM_NAME_MAX_LENGTH - 1);
    gs->group1[TEAM_NAME_MAX_LENGTH - 1] = '\0';
    gs->group2[TEAM_NAME_MAX_LENGTH - 1] = '\0';

    for (int i = 0; i < (int)NUM_COUNTRIES; i++)
        free(shuffled[i]);
}

void format_game_update(char *update, size_t buf_size, const GameState *gs) {
    int ret = snprintf(update, buf_size,
                       "Minute %d: Team %s: %d, Team %s: %d\n",
                       gs->current_minute,
                       gs->group1, gs->score[0],
                       gs->group2, gs->score[1]);
    if (ret >= (int)buf_size)
        snprintf(update, buf_size,
                 "Update message too long, some data was truncated.\n");
}

void *simulate_game(void *arg) {
    ServerContext *ctx = (ServerContext *)arg;
    int socket_fd = socket_fd_global;

    pthread_mutex_lock(&lock);
    ctx->game_state.current_minute = 0;
    ctx->game_state.score[0] = 0;
    ctx->game_state.score[1] = 0;
    ctx->game_state.game_running = 1;
    ctx->game_state.phase = GAME_PHASE_FIRST_HALF;
    pthread_mutex_unlock(&lock);

    printf("THE GAME HAS STARTED!\n");

    for (int j = 1; j <= GAME_LENGTH; j++) {
        pthread_mutex_lock(&lock);
        ctx->game_state.current_minute = j;
        int scorer = rand() % 2;
        int goal = rand() % 2;
        ctx->game_state.score[scorer] += goal;
        pthread_mutex_unlock(&lock);

        char update[BUFFER_SIZE];
        format_game_update(update, BUFFER_SIZE, &ctx->game_state);
        broadcast_game_update(update);
        printf("%s", update);

        if (j == GAME_LENGTH / 2) {
            printf("HALF TIME IN THE SIMULATION\n");
            pthread_mutex_lock(&lock);
            ctx->game_state.phase = GAME_PHASE_HALFTIME;
            pthread_mutex_unlock(&lock);
            broadcast_half_time_message(ctx);
            sleep(HalfTimer_respose);

            pthread_mutex_lock(&lock);
            ctx->game_state.phase = GAME_PHASE_SECOND_HALF;
            for (int i = 0; i < client_count; i++) {
                if (clients[i] && !clients[i]->recive_halftime) {
                    printf("Client %d did not respond to halftime, assuming 'NO'.\n",
                           clients[i]->client_id);
                    clients[i]->recive_halftime = 1;
                }
            }
            pthread_mutex_unlock(&lock);
        }

        sleep(1);
    }

    pthread_mutex_lock(&lock);
    ctx->game_state.game_running = 0;
    ctx->game_state.phase = GAME_PHASE_OVER;

    /* Snapshot clients under the lock, then enqueue finals to the pool */
    Client *snapshot[MAX_CLIENTS];
    int n = client_count;
    for (int i = 0; i < n; i++)
        snapshot[i] = clients[i];
    pthread_mutex_unlock(&lock);

    sleep(1);

    for (int i = 0; i < n; i++) {
        if (!snapshot[i])
            continue;
        Job job = {
            .type = JOB_SEND_FINAL,
            .client = snapshot[i],
            .message = NULL,
            .wrong_message = test_multicast_to_wrong_reciver,
            .epoll_fd = -1
        };
        if (pool_submit(&job) != 0)
            send_final_message(snapshot[i], test_multicast_to_wrong_reciver);
    }

    sleep(2);

    pthread_mutex_lock(&lock);
    client_count = 0;
    game_over = 1;
    pthread_mutex_unlock(&lock);

    if (socket_fd >= 0)
        close(socket_fd);

    return NULL;
}
