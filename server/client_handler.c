/* client_handler.c — epoll reactor drains sockets; workers process messages */
#include "server.h"
#include "thread_pool.h"

void handle_client_event(int client_fd, int epoll_fd) {
    char buffer[BUFFER_SIZE];

    while (1) {
        int bytes = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            if (errno == EINTR)
                continue;
            /* fall through to disconnect */
            bytes = 0;
        }

        if (bytes == 0) {
            Client *client = find_client_by_socket(client_fd);
            if (client) {
                printf("Client %d disconnected.\n", client->client_id);
                Job job = {
                    .type = JOB_DISCONNECT,
                    .client = client,
                    .message = NULL,
                    .wrong_message = 0,
                    .epoll_fd = epoll_fd
                };
                if (pool_submit(&job) != 0) {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    close(client_fd);
                    client->socket = -1;
                    client->connected = 0;
                    client->state = CLIENT_DISCONNECTED;
                }
            } else {
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                close(client_fd);
            }
            return;
        }

        buffer[bytes] = '\0';

        Client *client = find_client_by_socket(client_fd);
        if (!client)
            return;

        char *msg = malloc((size_t)bytes + 1);
        if (!msg)
            return;
        memcpy(msg, buffer, (size_t)bytes + 1);

        Job job = {
            .type = JOB_PROCESS_MESSAGE,
            .client = client,
            .message = msg,
            .wrong_message = 0,
            .epoll_fd = epoll_fd
        };
        if (pool_submit(&job) != 0) {
            free(msg);
            return;
        }
    }
}

void handle_new_connection(int socket_fd, int epoll_fd, ServerContext *ctx) {
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    /* Drain the listen backlog — accept until EAGAIN */
    while (1) {
        int client_fd = accept(socket_fd, (struct sockaddr *)&address, &addrlen);

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            if (errno == EINTR)
                continue;
            perror("accept");
            break;
        }

        make_socket_nonblocking(client_fd);

        pthread_mutex_lock(&lock);

        if (ctx->game_state.game_running) {
            const char *msg = "The game has already started. You cannot join now.\n";
            send(client_fd, msg, strlen(msg), 0);
            close(client_fd);
            pthread_mutex_unlock(&lock);
            continue;
        }

        if (client_count >= MAX_CLIENTS) {
            const char *msg = "Server is full. Try again later.\n";
            send(client_fd, msg, strlen(msg), 0);
            close(client_fd);
            pthread_mutex_unlock(&lock);
            continue;
        }

        Client *client = calloc(1, sizeof(Client));
        if (!client) {
            perror("calloc");
            close(client_fd);
            pthread_mutex_unlock(&lock);
            continue;
        }

        client->socket = client_fd;
        client->address = address;
        client->client_id = client_count;
        client->connected = 0;
        client->bet_received = 0;
        client->recive_halftime = 0;
        client->last_keep_alive = time(NULL);
        client->ctx = ctx;
        client->state = CLIENT_WAIT_AUTH;

        clients[client_count] = client;
        client_count++;

        pthread_mutex_unlock(&lock);

        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = client_fd;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
            perror("epoll_ctl client_fd");
            close(client_fd);
            client->socket = -1;
            client->state = CLIENT_DISCONNECTED;
            client->connected = 0;
            continue;
        }

        char buffer[BUFFER_SIZE];
        int remaining_time =
            GAME_DURATION - (int)difftime(time(NULL), ctx->start_time);
        if (remaining_time < 0)
            remaining_time = 0;

        snprintf(buffer, BUFFER_SIZE, "WELCOME_DATA:%s:%s:%d:%d",
                 ctx->game_state.group1,
                 ctx->game_state.group2,
                 remaining_time,
                 GAME_LENGTH);

        if (!test_drop_password)
            send(client_fd, buffer, strlen(buffer), 0);
        else
            printf("Simulating dropped password prompt for client %d.\n",
                   client->client_id);

        printf("Client %d connected using epoll+pool.\n", client->client_id);
    }
}

void send_final_message(Client *client, int wrong_message) {
    if (!client || !client->connected || client->socket < 0)
        return;

    char result[BUFFER_SIZE];
    GameState *gs = &client->ctx->game_state;
    const char *correct_group = (client->bet_team == 0) ? "tie"
                              : (client->bet_team == 1) ? gs->group1
                                                        : gs->group2;

    printf("Preparing to send final message to client %d.\n", client->client_id);

    if (wrong_message) {
        Client temp = *client;
        temp.bet_team = (client->bet_team + 1) % 3;
        const char *wg = (temp.bet_team == 0) ? "tie"
                       : (temp.bet_team == 1) ? gs->group1
                                              : gs->group2;
        int won = (temp.bet_team == 1 && gs->score[0] > gs->score[1]) ||
                  (temp.bet_team == 2 && gs->score[1] > gs->score[0]) ||
                  (temp.bet_team == 0 && gs->score[0] == gs->score[1]);
        snprintf(result, BUFFER_SIZE, won
                 ? "Congratulations! You won your bet of %d $ on %s\n"
                 : "Sorry, you lost your bet of %d $ on %s\n",
                 temp.bet_amount, wg);
        printf("Simulating wrong message for client %d.\n", client->client_id);
    } else {
        int won = (client->bet_team == 1 && gs->score[0] > gs->score[1]) ||
                  (client->bet_team == 2 && gs->score[1] > gs->score[0]) ||
                  (client->bet_team == 0 && gs->score[0] == gs->score[1]);
        snprintf(result, BUFFER_SIZE, won
                 ? "Congratulations! You won your bet of %d $ on %s\n"
                 : "Sorry, you lost your bet of %d $ on %s\n",
                 client->bet_amount, correct_group);
        printf("Sent correct final message to client %d: %s\n",
               client->client_id, result);
    }

    send(client->socket, result, strlen(result), 0);
    printf("Sent final message to client %d.\n", client->client_id);

    /* Half-close so the client can still drain the final payload */
    shutdown(client->socket, SHUT_WR);
    client->connected = 0;
    client->state = CLIENT_DISCONNECTED;
    printf("Client %d final sent (write half-closed).\n", client->client_id);
}

Client *find_client_by_socket(int socket) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i] && clients[i]->socket == socket)
            return clients[i];
    }
    return NULL;
}

static void strip_trailing_ws(char *s) {
    if (!s)
        return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) {
        s[n - 1] = '\0';
        n--;
    }
}

void handle_auth_message(Client *client, char *buffer) {
    strip_trailing_ws(buffer);
    if (strncmp(buffer, "AUTH:", 5) != 0) {
        printf("Unexpected auth message from client %d: %s\n",
               client->client_id, buffer);
        return;
    }

    char *password = buffer + 5;
    strip_trailing_ws(password);

    if (strcmp(password, SECRET_PASSWORD) != 0) {
        const char *msg = "Incorrect password. Connection closed.\n";
        send(client->socket, msg, strlen(msg), 0);
        close(client->socket);
        client->socket = -1;
        client->state = CLIENT_DISCONNECTED;
        return;
    }

    char msg[BUFFER_SIZE];
    snprintf(msg, BUFFER_SIZE,
             "Password accepted. Place your bet (0): tie, (1): %s, (2): %s) and amount (BY DOLLARS): ",
             client->ctx->game_state.group1,
             client->ctx->game_state.group2);

    send(client->socket, msg, strlen(msg), 0);
    client->state = CLIENT_WAIT_BET;
    printf("Client %d authenticated successfully.\n", client->client_id);
}

void handle_bet_message(Client *client, char *buffer) {
    strip_trailing_ws(buffer);
    if (sscanf(buffer, "%d %d", &client->bet_team, &client->bet_amount) != 2) {
        printf("Invalid bet from client %d: %s\n", client->client_id, buffer);
        return;
    }

    client->connected = 1;
    client->bet_received = 1;
    client->state = CLIENT_IN_GAME;

    log_client_message(client, buffer);

    printf("Client %d placed bet: team %d, amount %d.\n",
           client->client_id, client->bet_team, client->bet_amount);
}

void handle_game_message(Client *client, char *buffer) {
    strip_trailing_ws(buffer);

    if (strncmp(buffer, "KEEP_ALIVE:", 11) == 0) {
        client->last_keep_alive = time(NULL);
    } else if (strstr(buffer, "CLIENT_TERMINATED")) {
        printf("Client %d terminated connection.\n", client->client_id);
        client->connected = 0;
        client->state = CLIENT_DISCONNECTED;
        if (client->socket >= 0) {
            close(client->socket);
            client->socket = -1;
        }
    } else if (strstr(buffer, "REQUEST_HALFTIME_MESSAGE")) {
        const char *msg =
            "HALFTIME: Do you want to double your bet? Reply with 'YES' or 'NO'.\n";
        send(client->socket, msg, strlen(msg), 0);
    } else if (strstr(buffer, "YES") || strstr(buffer, "NO")) {
        client->recive_halftime = 1;
        if (strstr(buffer, "YES"))
            client->bet_amount *= 2;
        printf("Client %d chose %s at halftime.\n",
               client->client_id, strstr(buffer, "YES") ? "YES" : "NO");
    } else if (strncmp(buffer, "REQUEST_FINAL_MESSAGE", 21) == 0) {
        send_final_message(client, 0);
    } else if (strstr(buffer, "REQUEST_GAME_STATE")) {
        /* On-demand pull of current game state over TCP - a reliable
         * complement to the (unreliable, goal-only) UDP broadcast, available
         * at any time in normal operation, not gated to any test flag. */
        char state_msg[BUFFER_SIZE];
        pthread_mutex_lock(&lock);
        format_game_update(state_msg, BUFFER_SIZE, &client->ctx->game_state);
        pthread_mutex_unlock(&lock);
        send(client->socket, state_msg, strlen(state_msg), 0);
        printf("Client %d requested game state.\n", client->client_id);
    } else {
        printf("[UNCLASSIFIED] Client %d sent: %s\n", client->client_id, buffer);
    }
}
