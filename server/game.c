/* game.c */
#include "server.h"
#include "thread_pool.h"
#include <math.h>
#include <stdint.h>

#define BASE_XG            1.4
#define RATING_SCALE       800.0
#define POISSON_MAX_GOALS  8
#define ODDS_MIN           1.10
#define ODDS_MAX           15.00

typedef struct {
    const char *name;
    int rating;
} Country;

static const Country countries[NUM_COUNTRIES] = {
    {"Brazil", 2150},
    {"Argentina", 2140},
    {"France", 2080},
    {"Spain", 2070},
    {"England", 2040},
    {"Germany", 2020},
    {"Portugal", 2000},
    {"Netherlands", 1980},
    {"Italy", 1960},
    {"Belgium", 1930}
};

static void shuffle_countries(Country *arr, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        Country temp = arr[i];
        arr[i] = arr[j];
        arr[j] = temp;
    }
}

/* P(K = k) for a Poisson random variable. */
static double poisson_pmf(int k, double lambda) {
    double p = exp(-lambda);
    for (int i = 1; i <= k; i++)
        p *= lambda / (double)i;
    return p;
}

static double clamp_odds(double probability) {
    if (probability <= 0.0)
        return ODDS_MAX;
    double odds = 1.0 / probability;
    if (odds < ODDS_MIN)
        return ODDS_MIN;
    if (odds > ODDS_MAX)
        return ODDS_MAX;
    return odds;
}

/*
 * Expected goals grow with the rating gap. Win, loss, and draw probabilities
 * are the independent Poisson score matrix (0..8), renormalized so the three
 * outcomes sum to 1. Decimal odds are the reciprocal, clamped to a sane range.
 */
static void compute_match_odds(GameState *gs) {
    double gap = (double)(gs->rating1 - gs->rating2);
    gs->lambda1 = BASE_XG * pow(10.0, gap / RATING_SCALE);
    gs->lambda2 = BASE_XG * pow(10.0, -gap / RATING_SCALE);

    double p1 = 0.0, p2 = 0.0, pd = 0.0;
    for (int i = 0; i <= POISSON_MAX_GOALS; i++) {
        double pi = poisson_pmf(i, gs->lambda1);
        for (int j = 0; j <= POISSON_MAX_GOALS; j++) {
            double p = pi * poisson_pmf(j, gs->lambda2);
            if (i > j)
                p1 += p;
            else if (j > i)
                p2 += p;
            else
                pd += p;
        }
    }

    double total = p1 + p2 + pd;
    if (total > 0.0) {
        p1 /= total;
        p2 /= total;
        pd /= total;
    }

    gs->odds_team1 = clamp_odds(p1);
    gs->odds_team2 = clamp_odds(p2);
    gs->odds_tie = clamp_odds(pd);

    printf("Matchup %s (rating %d, xG %.2f) @ %.2f vs %s (rating %d, xG %.2f) @ %.2f, tie @ %.2f\n",
           gs->group1, gs->rating1, gs->lambda1, gs->odds_team1,
           gs->group2, gs->rating2, gs->lambda2, gs->odds_team2,
           gs->odds_tie);
}

void assign_teams(GameState *gs) {
    Country shuffled[NUM_COUNTRIES];
    for (int i = 0; i < (int)NUM_COUNTRIES; i++)
        shuffled[i] = countries[i];

    shuffle_countries(shuffled, NUM_COUNTRIES);

    strncpy(gs->group1, shuffled[0].name, TEAM_NAME_MAX_LENGTH - 1);
    strncpy(gs->group2, shuffled[1].name, TEAM_NAME_MAX_LENGTH - 1);
    gs->group1[TEAM_NAME_MAX_LENGTH - 1] = '\0';
    gs->group2[TEAM_NAME_MAX_LENGTH - 1] = '\0';
    gs->rating1 = shuffled[0].rating;
    gs->rating2 = shuffled[1].rating;

    compute_match_odds(gs);
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
    /* Per-minute goal chance matches the expected goals the odds were built from. */
    double p_goal1 = ctx->game_state.lambda1 / (double)GAME_LENGTH;
    double p_goal2 = ctx->game_state.lambda2 / (double)GAME_LENGTH;
    pthread_mutex_unlock(&lock);

    printf("THE GAME HAS STARTED!\n");

    for (int j = 1; j <= GAME_LENGTH; j++) {
        pthread_mutex_lock(&lock);
        ctx->game_state.current_minute = j;
        double roll1 = (double)rand() / ((double)RAND_MAX + 1.0);
        double roll2 = (double)rand() / ((double)RAND_MAX + 1.0);
        int goal1 = roll1 < p_goal1;
        int goal2 = roll2 < p_goal2;
        ctx->game_state.score[0] += goal1;
        ctx->game_state.score[1] += goal2;
        pthread_mutex_unlock(&lock);

        if (goal1 || goal2) {
            char update[BUFFER_SIZE];
            format_game_update(update, BUFFER_SIZE, &ctx->game_state);
            broadcast_game_update(update);
            printf("%s", update);
        }

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
