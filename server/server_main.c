#include "server.h"
#include "thread_pool.h"

int main(void) {
    srand(time(NULL));

    signal(SIGINT,  handle_signal);
    signal(SIGTSTP, handle_signal);

    pthread_mutex_init(&lock, NULL);
    thread_pool_init(&g_pool, NUM_WORKERS);

    // ─── Create TCP socket ────────────────────────────────────────────────────
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    server_fd_global = server_fd;  
    printf("TCP socket created.\n");

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // ─── Bind ─────────────────────────────────────────────────────────────────
    struct sockaddr_in address = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(PORT)
    };

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("Socket bound on port %d.\n", PORT);

    // ─── UDP Multicast ────────────────────────────────────────────────────────
    setup_udp_multicast();
    printf("UDP multicast ready.\n");

    // ─── Listen ───────────────────────────────────────────────────────────────
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("Server listening on port %d.\n", PORT);

    // ─── Main Loop ────────────────────────────────────────────────────────────
    accept_bets(server_fd);
    close_all_client_sockets();
    thread_pool_shutdown(&g_pool);

    pthread_exit(NULL);
    return 0;
}