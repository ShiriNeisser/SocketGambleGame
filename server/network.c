#include "server.h"

// ─── UDP Multicast Setup ──────────────────────────────────────────────────────

void setup_udp_multicast(void) {
    if ((udp_multicast_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("UDP socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin_family      = AF_INET;
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_GROUP);
    multicast_addr.sin_port        = htons(MULTICAST_PORT);
}

// ─── Broadcast Helpers ────────────────────────────────────────────────────────

void broadcast_game_update(const char *message) {
    if (sendto(udp_multicast_socket, message, strlen(message), 0,
               (struct sockaddr *)&multicast_addr, sizeof(multicast_addr)) < 0)
        perror("UDP multicast sendto failed");
    else
        printf("Broadcasted: %s", message);
}

void broadcast_half_time_message(void) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE,
             "HALFTIME: Do you want to double your bet? Reply with 'YES' or 'NO'.\n");

    pthread_mutex_lock(&lock);
    for (int i = 0; i < client_count; i++) {
        if (!clients[i]->connected) continue;
        if (test_drop_halftime) {
            printf("Simulating dropped halftime message for client %d.\n", clients[i]->client_id);
            continue;
        }
        send(clients[i]->socket, buffer, strlen(buffer), 0);
        printf("Sent halftime message to client %d.\n", clients[i]->client_id);
    }
    game_state.halftime = 1;
    pthread_mutex_unlock(&lock);

    printf("Broadcasted halftime message to all clients.\n");
}

// ─── Countdown Broadcast Thread ───────────────────────────────────────────────

void *broadcast_remaining_time(void *arg) {
    (void)arg;
    char buffer[BUFFER_SIZE];

    while (1) {
        pthread_mutex_lock(&lock);
        int remaining = GAME_DURATION - (int)difftime(time(NULL), start_time);
        pthread_mutex_unlock(&lock);

        if (remaining < 0) remaining = 0;

        snprintf(buffer, BUFFER_SIZE,
                 "Time remaining until the game starts: %d seconds\n", remaining);
        broadcast_game_update(buffer);
        printf("Broadcasted remaining time: %d seconds\n", remaining);

        if (remaining == 0 && !game_state.game_running) {
            start_game();
            break;
        }
        sleep(1);
    }
    return NULL;
}

// ─── Start Game ───────────────────────────────────────────────────────────────

void start_game(void) {
    pthread_t thread;
    pthread_create(&thread, NULL, simulate_game, (void*)(intptr_t)server_fd_global);
}

// ─── Accept & Register New Clients ───────────────────────────────────────────

void accept_bets(int server_fd) {
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    start_time = time(NULL);
    assign_teams(&game_state);

    pthread_t broadcast_thread;
    pthread_create(&broadcast_thread, NULL, broadcast_remaining_time, NULL);

    while (1) {
        int new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (new_socket < 0) {
            if (game_over) break;  
             continue;
             }
    
        pthread_mutex_lock(&lock);
        if (game_state.game_running) {
            const char *msg = "The game has already started. You cannot join now.\n";
            send(new_socket, msg, strlen(msg), 0);
            close(new_socket);
            printf("Client attempted to join after game started.\n");
            pthread_mutex_unlock(&lock);
            continue;
        }
        Client *client            = malloc(sizeof(Client));
        client->socket            = new_socket;
        client->address           = address;
        client->client_id         = client_count++;
        client->bet_received      = 0;
        client->recive_halftime   = 0;
        client->connected         = 0;
        client->last_keep_alive   = time(NULL);
        memset(client->comments, 0, BUFFER_SIZE);
        clients[client->client_id] = client;
        printf("Client %d connected.\n", client->client_id);
        pthread_mutex_unlock(&lock);

        pthread_t client_thread;
        pthread_create(&client_thread, NULL, handle_new_client, client);
        pthread_detach(client_thread);
    }

    pthread_join(broadcast_thread, NULL);
}

// ─── Cleanup ─────────────────────────────────────────────────────────────────

void close_all_client_sockets(void) {
    for (int i = 0; i < client_count; i++) {
        close(clients[i]->socket);
        printf("Closed socket for client %d.\n", clients[i]->client_id);
    }
}

void notify_clients_of_interruption(void) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "The game has been interrupted by the server.\n");
    for (int i = 0; i < client_count; i++) {
        send(clients[i]->socket, buffer, strlen(buffer), 0);
        close(clients[i]->socket);
    }
    printf("All clients notified of interruption.\n");
}

void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTSTP) {
        printf("\nGame interrupted by server.\n");
        notify_clients_of_interruption();
        close_all_client_sockets();
        close(udp_multicast_socket);
        pthread_exit(NULL);
    }
}

// ─── Logging ─────────────────────────────────────────────────────────────────

void log_client_message(Client *client, const char *message) {
    snprintf(client->comments, BUFFER_SIZE, "Client %d sent: %s", client->client_id, message);
    printf("%s\n", client->comments);
}