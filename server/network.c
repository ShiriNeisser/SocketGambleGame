/*network.c*/
#include "server.h"

static int                udp_multicast_socket;
static struct sockaddr_in multicast_addr;
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

void broadcast_half_time_message(ServerContext *ctx) {
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
    ctx->game_state.halftime = 1;
    pthread_mutex_unlock(&lock);

    printf("Broadcasted halftime message to all clients.\n");
}

// ─── Countdown Broadcast Thread ───────────────────────────────────────────────

void *broadcast_remaining_time(void *arg) {
    ServerContext *ctx = (ServerContext *)arg;
    char buffer[BUFFER_SIZE];

    while (1) {
        pthread_mutex_lock(&lock);
        int remaining = GAME_DURATION - (int)difftime(time(NULL), ctx->start_time);
        pthread_mutex_unlock(&lock);

        if (remaining < 0) remaining = 0;

        snprintf(buffer, BUFFER_SIZE,
                 "Time remaining until the game starts: %d seconds\n", remaining);
        broadcast_game_update(buffer);
        printf("Broadcasted remaining time: %d seconds\n", remaining);

        if (remaining == 0 && !ctx->game_state.game_running) {
            start_game(ctx);
            break;
        }
        sleep(1);
    }
    return NULL;
}

// ─── Start Game ───────────────────────────────────────────────────────────────

void start_game(ServerContext *ctx) {
    pthread_t thread;
    pthread_create(&thread, NULL, simulate_game, ctx);
}

// ─── Accept & Register New Clients ───────────────────────────────────────────

void make_socket_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        exit(EXIT_FAILURE);
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        exit(EXIT_FAILURE);
    }
}

void accept_bets(int socket_fd, ServerContext *ctx) {
    ctx->start_time = time(NULL);

    make_socket_nonblocking(socket_fd);

    pthread_t broadcast_thread;
    pthread_create(&broadcast_thread, NULL, broadcast_remaining_time, ctx);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = socket_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &ev) == -1) {
        perror("epoll_ctl socket_fd");
        exit(EXIT_FAILURE);
    }

    struct epoll_event events[MAX_EVENTS];

    while (!game_over) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == socket_fd) {
                handle_new_connection(socket_fd, epoll_fd, ctx);
            } 
            else {
                handle_client_event(fd,epoll_fd);
            }
        }
    }
    close(epoll_fd);
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