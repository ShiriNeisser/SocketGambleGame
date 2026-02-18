#include "client.h"

// ─── Signal Handler ───────────────────────────────────────────────────────────

void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTSTP) {
        printf("\nYou are paying %d\n", bet_amount);
        char buf[BUFFER_SIZE];
        snprintf(buf, BUFFER_SIZE, "CLIENT_TERMINATED %d", bet_amount);
        send(tcp_socket, buf, strlen(buf), 0);
        stop_udp_listener = 1;
        close(tcp_socket);
        close(udp_multicast_socket);
        exit(0);
    }
}

// ─── Server Interruption ──────────────────────────────────────────────────────

void handle_interruption(void) {
    printf("\nGame interrupted by server. Closing and exiting...\n");
    close(tcp_socket);
    close(udp_multicast_socket);
    exit(0);
}

// ─── Main TCP Message Loop ────────────────────────────────────────────────────

int process_server_messages(int sock, pthread_t update_thread) {
    char buf[BUFFER_SIZE];
    char received_group[TEAM_NAME_MAX_LENGTH];

    while (1) {
        int bytes = read(sock, buf, BUFFER_SIZE);
        if (bytes <= 0) break;
        buf[bytes] = '\0';

        if (strstr(buf, "interrupted")) {
            handle_interruption();

        } else if (strstr(buf, "HALFTIME")) {
            halftime_received = 1;
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            printf("Do you want to double your bet? (YES/NO): ");
            fflush(stdout);
            char response[BUFFER_SIZE];
            scanf("%s", response);

            if (strcmp(response, "YES") != 0 && strcmp(response, "NO") != 0) {
                printf("Invalid response, defaulting to 'NO'.\n");
                strcpy(response, "NO");
            }

            printf("Sending response: %s\n", response);
            send(sock, response, strlen(response), 0);

        } else if (strstr(buf, "Congratulations!") || strstr(buf, "Sorry")) {
            // Validate that the final message matches our bet
            int matched =
                sscanf(buf, "Congratulations! You won your bet of %*d $ on %99s", received_group) == 1 ||
                sscanf(buf, "Sorry, you lost your bet of %*d $ on %99s",           received_group) == 1;

            if (matched) {
                if (strcmp(my_bet.my_group, received_group) == 0) {
                    if (DebugMode) printf("Final message matches bet.\n");
                    printf("%s", buf);
                    break; // Correct message received – done
                } else {
                    printf("Received incorrect final message. Requesting correct one.\n");
                    const char *req = "REQUEST_FINAL_MESSAGE";
                    send(sock, req, strlen(req), 0);
                }
            } else {
                printf("Could not parse final message: %s\n", buf);
            }

        } else {
            printf("[TCP] %s", buf);
        }
    }

    stop_udp_listener = 1;
    pthread_join(update_thread, NULL);
    close(sock);
    printf("Socket closed after receiving all updates.\n");
    return 0;
}

// ─── Keep-Alive Thread (currently disabled) ───────────────────────────────────
/*
void *send_keep_alive(void *arg) {
    while (1) {
        if (!test_keepalive_not_recived) {
            if (send(tcp_socket, "KEEP_ALIVE:", strlen("KEEP_ALIVE:"), 0) < 0) {
                perror("Keep-alive send failed");
                break;
            }
            if (show_keep_alive_print) printf("Sent keep-alive to server.\n");
        } else {
            printf("Keep-alive suppressed (test flag).\n");
            break;
        }
        sleep(5);
    }
    close(tcp_socket);
    close(udp_multicast_socket);
    printf("Exiting due to server disconnection.\n");
    exit(1);
    return NULL;
}
*/