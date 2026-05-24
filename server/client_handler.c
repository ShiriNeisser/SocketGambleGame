/*
client_handler.c*/
#include "server.h"

// ─── Ongoing Client Requests (after bet placed) ───────────────────────────────

void handle_client_event(int client_fd, int epoll_fd) {
    char buffer[BUFFER_SIZE];

    int bytes = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

    if (bytes <= 0) {
        Client *client = find_client_by_socket(client_fd);

        if (client) {
            printf("Client %d disconnected.\n", client->client_id);
            client->connected = 0;
            client->state = CLIENT_DISCONNECTED;
        }

        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
        close(client_fd);
        return;
    }

    buffer[bytes] = '\0';

    Client *client = find_client_by_socket(client_fd);
    if (!client) return;

    switch (client->state) {
        case CLIENT_WAIT_AUTH:
            handle_auth_message(client, buffer);
            break;

        case CLIENT_WAIT_BET:
            handle_bet_message(client, buffer);
            break;

        case CLIENT_IN_GAME:
            handle_game_message(client, buffer);
            break;

        default:
            break;
    }
}
// ─── New Client Handshake (welcome → password → bet) ─────────────────────────

void handle_new_connection(int socket_fd, int epoll_fd, ServerContext *ctx) {
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    int client_fd = accept(socket_fd, (struct sockaddr *)&address, &addrlen);

    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("accept");
        }
        return;
    }

    make_socket_nonblocking(client_fd);

    pthread_mutex_lock(&lock);

    if (ctx->game_state.game_running) {
        const char *msg = "The game has already started. You cannot join now.\n";
        send(client_fd, msg, strlen(msg), 0);
        close(client_fd);
        pthread_mutex_unlock(&lock);
        return;
    }

    if (client_count >= MAX_CLIENTS) {
        const char *msg = "Server is full. Try again later.\n";
        send(client_fd, msg, strlen(msg), 0);
        close(client_fd);
        pthread_mutex_unlock(&lock);
        return;
    }

    Client *client = calloc(1, sizeof(Client));
    if (!client) {
        perror("calloc");
        close(client_fd);
        pthread_mutex_unlock(&lock);
        return;
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
        client->state = CLIENT_DISCONNECTED;
        client->connected = 0;
        return;
    }

    char buffer[BUFFER_SIZE];
    int remaining_time = GAME_DURATION - (int)difftime(time(NULL), ctx->start_time);

    if (remaining_time < 0) {
        remaining_time = 0;
    }

    snprintf(buffer, BUFFER_SIZE, "WELCOME_DATA:%s:%s:%d",
             ctx->game_state.group1,
             ctx->game_state.group2,
             remaining_time);

    if (!test_drop_password) {
        send(client_fd, buffer, strlen(buffer), 0);
    } else {
        printf("Simulating dropped password prompt for client %d.\n", client->client_id);
    }

    printf("Client %d connected using epoll.\n", client->client_id);
}
// ─── Send Final Result Message ────────────────────────────────────────────────

void send_final_message(Client *client, int wrong_message) {
    if (!client->connected)
        return;

    char result[BUFFER_SIZE];
    GameState *gs = &client->ctx->game_state;
    const char *correct_group = (client->bet_team == 0) ? "tie"
                              : (client->bet_team == 1) ? gs->group1
                              :                           gs->group2;

    printf("Preparing to send final message to client %d.\n", client->client_id);

    if (wrong_message) {
        // Simulate sending the wrong result (bet_team shifted by 1)
        Client temp      = *client;
        temp.bet_team    = (client->bet_team + 1) % 3;
        const char *wg   = (temp.bet_team == 0) ? "tie"
                         : (temp.bet_team == 1) ? gs->group1
                         :                        gs->group2;
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
        printf("Sent correct final message to client %d: %s\n", client->client_id, result);
    }

    send(client->socket, result, strlen(result), 0);
    printf("Sent final message to client %d.\n", client->client_id);

    sleep(1);
    close(client->socket);
    printf("Client %d disconnected. Socket closed.\n", client->client_id);
}

Client *find_client_by_socket(int socket) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i] && clients[i]->socket == socket) {
            return clients[i];
        }
    }
    return NULL;
}

void handle_auth_message(Client *client, char *buffer) {
    if (strncmp(buffer, "AUTH:", 5) != 0) {
        printf("Unexpected auth message from client %d: %s\n",
               client->client_id, buffer);
        return;
    }

    char *password = buffer + 5;

    if (strcmp(password, SECRET_PASSWORD) != 0) {
        const char *msg = "Incorrect password. Connection closed.\n";
        send(client->socket, msg, strlen(msg), 0);
        close(client->socket);
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
    if (sscanf(buffer, "%d %d", &client->bet_team, &client->bet_amount) != 2) {
        printf("Invalid bet from client %d: %s\n", client->client_id, buffer);
        return;
    }

    client->connected = 1;
    client->bet_received = 1;
    client->state = CLIENT_IN_GAME;

    log_client_message(client, buffer);

    printf("Client %d placed bet: team %d, amount %d.\n",
           client->client_id,
           client->bet_team,
           client->bet_amount);
}

void handle_game_message(Client *client, char *buffer) {
    if (strncmp(buffer, "KEEP_ALIVE:", 11) == 0) {
        client->last_keep_alive = time(NULL);
    }

    else if (strstr(buffer, "CLIENT_TERMINATED")) {
        printf("Client %d terminated connection.\n", client->client_id);
        client->connected = 0;
        client->state = CLIENT_DISCONNECTED;
        close(client->socket);
    }

    else if (strstr(buffer, "REQUEST_HALFTIME_MESSAGE")) {
        const char *msg =
            "HALFTIME: Do you want to double your bet? Reply with 'YES' or 'NO'.\n";
        send(client->socket, msg, strlen(msg), 0);
    }

    else if (strstr(buffer, "YES") || strstr(buffer, "NO")) {
        client->recive_halftime = 1;

        if (strstr(buffer, "YES")) {
            client->bet_amount *= 2;
        }

        printf("Client %d chose %s at halftime.\n",
               client->client_id,
               strstr(buffer, "YES") ? "YES" : "NO");
    }

    else if (strncmp(buffer, "REQUEST_FINAL_MESSAGE", 21) == 0) {
        send_final_message(client, 0);
    }

    else {
        printf("[UNCLASSIFIED] Client %d sent: %s\n",
               client->client_id, buffer);
    }
}