#include "client.h"

int main(void) {
    signal(SIGINT,  handle_signal);
    signal(SIGTSTP, handle_signal);

    // ─── UDP Multicast setup ──────────────────────────────────────────────────
    setup_udp_multicast();
    printf("UDP multicast ready.\n");

    // ─── TCP connection ───────────────────────────────────────────────────────
    tcp_socket = setup_tcp_connection();
    if (tcp_socket < 0) return -1;
    printf("Connected to server.\n");

    // ─── Start UDP listener thread ────────────────────────────────────────────
    pthread_t update_thread;
    pthread_create(&update_thread, NULL, listen_for_updates, NULL);

    /* Keep-alive thread (disabled):
    pthread_t keep_alive_thread;
    pthread_create(&keep_alive_thread, NULL, send_keep_alive, NULL);
    */

    // ─── Authenticate ─────────────────────────────────────────────────────────
    if (authenticate_with_server(tcp_socket) < 0) {
        printf("Authentication failed. Disconnecting.\n");
        stop_udp_listener = 1;
        close(tcp_socket);
        pthread_join(update_thread, NULL);
        return 1;
    }

    // ─── Place bet ────────────────────────────────────────────────────────────
    if (place_bet(tcp_socket) < 0) {
        printf("Bet placement failed. Disconnecting.\n");
        stop_udp_listener = 1;
        close(tcp_socket);
        pthread_join(update_thread, NULL);
        return 1;
    }

    // ─── Receive live updates ─────────────────────────────────────────────────
    ready_to_receive_updates = 1;
    process_server_messages(tcp_socket, update_thread);

    return 0;
}