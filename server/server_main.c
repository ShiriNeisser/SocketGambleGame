/*server_main.c*/
#include "server.h"

int main(void) {
    ServerContext ctx = {0};
    assign_teams(&ctx.game_state);
    ctx.game_state.phase = GAME_PHASE_PRE;
    srand((unsigned)time(NULL));

    pthread_mutex_init(&lock, NULL);

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    socket_fd_global = socket_fd;
    printf("TCP socket created.\n");

    int opt = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT)
    };

    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }
    printf("Socket bound on port %d.\n", PORT);

    setup_udp_multicast();
    printf("UDP multicast ready.\n");

    if (listen(socket_fd, LISTEN_BACKLOG) < 0) {
        perror("listen");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }
    printf("Server listening on port %d (backlog=%d, max_clients=%d).\n",
           PORT, LISTEN_BACKLOG, MAX_CLIENTS);

    accept_bets(socket_fd, &ctx);
    close_all_client_sockets();

    return 0;
}
