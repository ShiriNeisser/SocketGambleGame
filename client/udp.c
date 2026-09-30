#include "client.h"

// ─── Setup UDP Multicast ──────────────────────────────────────────────────────

void setup_udp_multicast(void) {
    if ((udp_multicast_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("UDP socket creation failed");
        exit(EXIT_FAILURE);
    }

    int reuse = 1;
    if (setsockopt(udp_multicast_socket, SOL_SOCKET, SO_REUSEADDR,
                   (char *)&reuse, sizeof(reuse)) < 0) {
        perror("Setting SO_REUSEADDR failed");
        close(udp_multicast_socket);
        exit(EXIT_FAILURE);
    }

    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin_family      = AF_INET;
    multicast_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    multicast_addr.sin_port        = htons(MULTICAST_PORT);

    if (bind(udp_multicast_socket, (struct sockaddr *)&multicast_addr,
             sizeof(multicast_addr)) < 0) {
        perror("UDP socket bind failed");
        close(udp_multicast_socket);
        exit(EXIT_FAILURE);
    }

    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP);
    mreq.imr_address.s_addr   = htonl(INADDR_ANY);
    mreq.imr_ifindex          = 0;

    if (setsockopt(udp_multicast_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   (char *)&mreq, sizeof(mreq)) < 0) {
        perror("Setting IP_ADD_MEMBERSHIP failed");
        close(udp_multicast_socket);
        exit(EXIT_FAILURE);
    }
}

// ─── UDP Listener Thread ──────────────────────────────────────────────────────

void *listen_for_updates(void *arg) {
    (void)arg;
    char buf[BUFFER_SIZE];

    while (!stop_udp_listener) {
        fd_set readfds;
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        FD_ZERO(&readfds);
        FD_SET(udp_multicast_socket, &readfds);

        int ret = select(udp_multicast_socket + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            perror("select()");
            break;
        }

        if (ret > 0) {
            int bytes = recv(udp_multicast_socket, buf, BUFFER_SIZE - 1, 0);
            if (bytes < 0) {
                if (stop_udp_listener) break;
                perror("UDP recv failed");
                break;
            }

            if (bytes > 0 && ready_to_receive_updates) {
                buf[bytes] = '\0';

                // ─── Dispatch by message type ─────────────────────────────
                if (strstr(buf, "interrupted")) {
                    handle_interruption();

                } else if (strstr(buf, "Minute")) {
                    if (DebugMode) printf("[UDP-GAME] ");
                    printf("%s", buf);

                } else if (strstr(buf, "remaining")) {
                    if (DebugMode) printf("[REMAINING] ");
                    printf("%s", buf);

                } else if (strstr(buf, "Congratulations!") || strstr(buf, "Sorry")) {
                    printf("[ERROR] Final result arrived via UDP instead of TCP!\n");

                } else {
                    printf("[UDP] %s", buf);
                }
            }
        }
    }

    close(udp_multicast_socket);
    printf("Multicast socket closed.\n");
    return NULL;
}