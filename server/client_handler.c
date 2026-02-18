#include "server.h"

// ─── Ongoing Client Requests (after bet placed) ───────────────────────────────

void *handle_client_requests(void *arg) {
    Client *client = (Client *)arg;
    char buffer[BUFFER_SIZE];

    while (1) {
        int bytes = recv(client->socket, buffer, BUFFER_SIZE, 0);

        if (bytes > 0) {
            buffer[bytes] = '\0';
            printf("Received '%s' from client %d\n", buffer, client->client_id);

            if (strncmp(buffer, "KEEP_ALIVE:", 11) == 0) {
                pthread_mutex_lock(&lock);
                client->last_keep_alive = time(NULL);
                pthread_mutex_unlock(&lock);
                if (print_keep_alive_prints)
                    printf("Keep-alive received from client %d.\n", client->client_id);

            } else if (strstr(buffer, "CLIENT_TERMINATED")) {
                printf("Client %d terminated the connection.\n", client->client_id);
                pthread_mutex_lock(&lock);
                client_count--;
                pthread_mutex_unlock(&lock);
                close(client->socket);
                client->connected = 0;
                return NULL;

            } else if (strstr(buffer, "REQUEST_HALFTIME_MESSAGE")) {
                pthread_mutex_lock(&lock);
                char msg[BUFFER_SIZE];
                snprintf(msg, BUFFER_SIZE,
                         "HALFTIME: Do you want to double your bet? Reply with 'YES' or 'NO'.\n");
                send(client->socket, msg, strlen(msg), 0);
                pthread_mutex_unlock(&lock);

            } else if (strstr(buffer, "YES") || strstr(buffer, "NO")) {
                pthread_mutex_lock(&lock);
                client->recive_halftime = 1;
                if (strstr(buffer, "YES"))
                    client->bet_amount *= 2;
                pthread_mutex_unlock(&lock);
                printf("Client %d chose to %s their bet.\n",
                       client->client_id, strstr(buffer, "YES") ? "double" : "not double");

            } else if (strncmp(buffer, "REQUEST_FINAL_MESSAGE", 21) == 0) {
                printf("Client %d requested the correct final message.\n", client->client_id);
                send_final_message(client, 0);

            } else if (sscanf(buffer, "%d %d", &client->bet_team, &client->bet_amount) == 2) {
                printf("Client %d placed a bet on team %d, amount %d.\n",
                       client->client_id, client->bet_team, client->bet_amount);

            } else {
                printf("[UNCLASSIFIED] Client %d sent: %s\n", client->client_id, buffer);
            }

        } else if (bytes == 0) {
            close(client->socket);
            printf("Client %d disconnected.\n", client->client_id);
            break;
        } else {
            perror("recv");
            close(client->socket);
            printf("Client %d disconnected due to error.\n", client->client_id);
            break;
        }
    }
    return NULL;
}

// ─── New Client Handshake (welcome → password → bet) ─────────────────────────

void *handle_new_client(void *arg) {
    Client *client = (Client *)arg;
    char buffer[BUFFER_SIZE];

    int remaining_time = GAME_DURATION - difftime(time(NULL), start_time);
    int ret = snprintf(buffer, BUFFER_SIZE, "WELCOME_DATA:%s:%s:%d",
                       game_state.group1, game_state.group2, remaining_time);
    if (ret >= BUFFER_SIZE)
        snprintf(buffer, BUFFER_SIZE, "WELCOME_DATA:Team1:Team2:0");

    if (!test_drop_password)
        send(client->socket, buffer, strlen(buffer), 0);
    else
        printf("Simulating dropped password prompt for client %d.\n", client->client_id);

    // ─── Wait for password ───────────────────────────────────────────────────
    while (1) {
        fd_set readfds;
        struct timeval tv = { .tv_sec = AUTH_TIMEOUT_SEC, .tv_usec = 0 };
        FD_ZERO(&readfds);
        FD_SET(client->socket, &readfds);

        int result = select(client->socket + 1, &readfds, NULL, NULL, &tv);
        if (result <= 0) {
            printf("Client %d timed out waiting for password.\n", client->client_id);
            snprintf(buffer, BUFFER_SIZE, "Timeout: No input received within 15 seconds. Connection closed.\n");
            send(client->socket, buffer, strlen(buffer), 0);
            close(client->socket);
            break;
        }

        int bytes = recv(client->socket, buffer, BUFFER_SIZE, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';

        // Silently ignore keep-alives during auth
        if (strncmp(buffer, "KEEP_ALIVE:", 11) == 0) {
            printf("Ignored KEEP_ALIVE from client %d during auth.\n", client->client_id);
            continue;
        }

        if (strncmp(buffer, "AUTH:", 5) != 0) {
            printf("Unexpected message from client %d during auth: %s\n",
                   client->client_id, buffer);
            continue;
        }

        // ─── Check password ──────────────────────────────────────────────────
        char *password = buffer + 5;
        if (strcmp(password, SECRET_PASSWORD) != 0) {
            snprintf(buffer, BUFFER_SIZE, "Incorrect password. Connection closed.\n");
            send(client->socket, buffer, strlen(buffer), 0);
            close(client->socket);
            printf("Client %d gave wrong password.\n", client->client_id);
            break;
        }

        ret = snprintf(buffer, BUFFER_SIZE,
                       "Password accepted. Place your bet (0): tie, (1): %s, (2): %s) and amount (BY DOLLARS): ",
                       game_state.group1, game_state.group2);
        if (ret >= BUFFER_SIZE)
            snprintf(buffer, BUFFER_SIZE, "Password accepted. Place your bet (0: tie, 1: Team 1, 2: Team 2) and amount: ");
        send(client->socket, buffer, strlen(buffer), 0);

        // ─── Wait for bet ────────────────────────────────────────────────────
        fd_set betfds;
        struct timeval btv = { .tv_sec = BET_TIMEOUT_SEC, .tv_usec = 0 };
        FD_ZERO(&betfds);
        FD_SET(client->socket, &betfds);

        result = select(client->socket + 1, &betfds, NULL, NULL, &btv);
        if (result <= 0) {
            printf("Client %d did not place a bet in time.\n", client->client_id);
            snprintf(buffer, BUFFER_SIZE, "Timeout: No bet placed within 15 seconds. Connection closed.\n");
            send(client->socket, buffer, strlen(buffer), 0);
            close(client->socket);
            break;
        }

        bytes = recv(client->socket, buffer, BUFFER_SIZE, 0);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            sscanf(buffer, "%d %d", &client->bet_team, &client->bet_amount);
            client->connected    = 1;
            client->bet_received = 1;
            log_client_message(client, buffer);

            pthread_t req_thread;
            pthread_create(&req_thread, NULL, handle_client_requests, client);
            pthread_detach(req_thread);
        }
        break;
    }

    return NULL;
}

// ─── Send Final Result Message ────────────────────────────────────────────────

void send_final_message(Client *client, int wrong_message) {
    if (!client->connected)
        return;

    char result[BUFFER_SIZE];
    const char *correct_group = (client->bet_team == 0) ? "tie"
                              : (client->bet_team == 1) ? game_state.group1
                              :                           game_state.group2;

    printf("Preparing to send final message to client %d.\n", client->client_id);

    if (wrong_message) {
        // Simulate sending the wrong result (bet_team shifted by 1)
        Client temp      = *client;
        temp.bet_team    = (client->bet_team + 1) % 3;
        const char *wg   = (temp.bet_team == 0) ? "tie"
                         : (temp.bet_team == 1) ? game_state.group1
                         :                        game_state.group2;
        int won = (temp.bet_team == 1 && game_state.score[0] > game_state.score[1]) ||
                  (temp.bet_team == 2 && game_state.score[1] > game_state.score[0]) ||
                  (temp.bet_team == 0 && game_state.score[0] == game_state.score[1]);
        snprintf(result, BUFFER_SIZE, won
                 ? "Congratulations! You won your bet of %d $ on %s\n"
                 : "Sorry, you lost your bet of %d $ on %s\n",
                 temp.bet_amount, wg);
        printf("Simulating wrong message for client %d.\n", client->client_id);
    } else {
        int won = (client->bet_team == 1 && game_state.score[0] > game_state.score[1]) ||
                  (client->bet_team == 2 && game_state.score[1] > game_state.score[0]) ||
                  (client->bet_team == 0 && game_state.score[0] == game_state.score[1]);
        snprintf(result, BUFFER_SIZE, won
                 ? "Congratulations! You won your bet of %d $ on %s\n"
                 : "Sorry, you lost your bet of %d $ on %s\n",
                 client->bet_amount, correct_group);
        printf("Sent correct final message to client %d: %s\n", client->client_id, result);
    }

    send(client->socket, result, strlen(result), 0);
    printf("Sent final message to client %d.\n", client->client_id);

    sleep(1);
    close(client->socket);
    printf("Client %d disconnected. Socket closed.\n", client->client_id);
}