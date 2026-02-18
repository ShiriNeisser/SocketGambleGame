#include "server.h"

// ─── Team Assignment ──────────────────────────────────────────────────────────

void shuffle_countries(char *shuffled[], int n) {
    for (int i = n - 1; i > 0; i--) {
        int j      = rand() % (i + 1);
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

// ─── Update Formatting ────────────────────────────────────────────────────────

void format_game_update(char *update, size_t buf_size, const GameState *gs) {
    int ret = snprintf(update, buf_size,
                       "Minute %d: Team %s: %d, Team %s: %d\n",
                       gs->current_minute,
                       gs->group1, gs->score[0],
                       gs->group2, gs->score[1]);
    if (ret >= (int)buf_size)
        snprintf(update, buf_size, "Update message too long, some data was truncated.\n");
}

// ─── Game Simulation Thread ───────────────────────────────────────────────────

void *simulate_game(void *arg) {
    int server_fd = (int)(intptr_t)arg;

    pthread_mutex_lock(&lock);
    game_state.current_minute = 0;
    game_state.score[0]       = 0;
    game_state.score[1]       = 0;
    game_state.game_running   = 1;
    pthread_mutex_unlock(&lock);

    printf("THE GAME HAS STARTED!\n");

    for (int j = 1; j <= GAME_LENGTH; j++) {
        pthread_mutex_lock(&lock);
        game_state.current_minute = j;
        int scorer = rand() % 2;
        int goal   = rand() % 2;
        game_state.score[scorer] += goal;
        pthread_mutex_unlock(&lock);

        char update[BUFFER_SIZE];
        format_game_update(update, BUFFER_SIZE, &game_state);
        broadcast_game_update(update);
        printf("%s", update);

        // ─── Halftime ────────────────────────────────────────────────────────
        if (j == GAME_LENGTH / 2) {
            printf("HALF TIME IN THE SIMULATION");
            broadcast_half_time_message();
            sleep(HalfTimer_respose);

            pthread_mutex_lock(&lock);
            for (int i = 0; i < client_count; i++) {
                if (!clients[i]->recive_halftime) {
                    printf("Client %d did not respond to halftime, assuming 'NO'.\n",
                           clients[i]->client_id);
                    clients[i]->recive_halftime = 1;
                }
            }
            pthread_mutex_unlock(&lock);
        }

        sleep(1);
    }

    // ─── End of Game ─────────────────────────────────────────────────────────
    pthread_mutex_lock(&lock);
    game_state.game_running = 0;
    pthread_mutex_unlock(&lock);

    sleep(2); // Give clients time to receive the last update

    for (int i = 0; i < client_count; i++) {
        send_final_message(clients[i], test_multicast_to_wrong_reciver);
        sleep(1);
    }
    client_count = 0;
    game_over = 1;
    close(server_fd); 
    pthread_exit(NULL);
}



// ─── Keep-Alive Check Thread (currently disabled) ────────────────────────────
/*
void *check_keep_alive(void *arg) {
    while (1) {
        pthread_mutex_lock(&lock);
        time_t now = time(NULL);
        for (int i = 0; i < client_count; i++) {
            if (clients[i]->connected && difftime(now, clients[i]->last_keep_alive) > 10) {
                printf("Client %d missed keep-alive, disconnecting.\n", clients[i]->client_id);
                clients[i]->connected = 0;
                const char *msg = "You have been disconnected due to inactivity.\n";
                send(clients[i]->socket, msg, strlen(msg), 0);
                close(clients[i]->socket);
                for (int j = i; j < client_count - 1; j++)
                    clients[j] = clients[j + 1];
                client_count--;
                i--;
            } else if (print_keep_alive_prints) {
                printf("Client %d keep-alive is OK.\n", clients[i]->client_id);
            }
        }
        pthread_mutex_unlock(&lock);
        sleep(5);
    }
    return NULL;
}
*/