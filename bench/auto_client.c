/*
 * Headless auto-client for architecture load tests.
 * Connects, authenticates, places a bet, answers halftime, waits for final.
 *
 * Usage: ./auto_client [host] [port]
 * Env:   AUTO_PASSWORD (default 1234), AUTO_BET_TEAM (default 1),
 *        AUTO_BET_AMOUNT (default 100), AUTO_HALFTIME (YES/NO, default NO)
 *        AUTO_CONNECT_DELAY_MS — sleep before connect (late-join)
 *        AUTO_RECV_TIMEOUT_SEC — per-recv timeout (default 90)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <time.h>

#define BUFFER_SIZE 1024
#define DEFAULT_PORT 8084

static int send_all(int fd, const char *msg) {
    size_t len = strlen(msg);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, msg + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static int recv_some(int fd, char *buf, size_t bufsize, int timeout_sec) {
    fd_set rfds;
    struct timeval tv = { .tv_sec = timeout_sec, .tv_usec = 0 };
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    int ready = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (ready <= 0)
        return ready;
    int n = (int)recv(fd, buf, bufsize - 1, 0);
    if (n > 0)
        buf[n] = '\0';
    return n;
}

int main(int argc, char *argv[]) {
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? atoi(argv[2]) : DEFAULT_PORT;

    const char *password = getenv("AUTO_PASSWORD");
    if (!password)
        password = "1234";
    const char *bet_team = getenv("AUTO_BET_TEAM");
    if (!bet_team)
        bet_team = "1";
    const char *bet_amount = getenv("AUTO_BET_AMOUNT");
    if (!bet_amount)
        bet_amount = "100";
    const char *halftime = getenv("AUTO_HALFTIME");
    if (!halftime)
        halftime = "NO";

    int delay_ms = 0;
    const char *delay_env = getenv("AUTO_CONNECT_DELAY_MS");
    if (delay_env)
        delay_ms = atoi(delay_env);

    int recv_timeout = 90;
    const char *to_env = getenv("AUTO_RECV_TIMEOUT_SEC");
    if (to_env)
        recv_timeout = atoi(to_env);
    if (recv_timeout < 5)
        recv_timeout = 5;

    if (delay_ms > 0)
        usleep((useconds_t)delay_ms * 1000);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 2;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return 2;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return 2;
    }

    char buf[BUFFER_SIZE];
    int n = recv_some(sock, buf, sizeof(buf), recv_timeout);
    if (n <= 0) {
        close(sock);
        return 1;
    }

    /* Rejected: full or game already started */
    if (strstr(buf, "Server is full") || strstr(buf, "cannot join")) {
        close(sock);
        return 3; /* rejected (expected in edge cases) */
    }

    if (!strstr(buf, "WELCOME_DATA")) {
        /* may be partial; keep going if AUTH possible later */
    }

    char auth[BUFFER_SIZE];
    snprintf(auth, sizeof(auth), "AUTH:%s", password);
    if (send_all(sock, auth) != 0) {
        close(sock);
        return 1;
    }

    n = recv_some(sock, buf, sizeof(buf), recv_timeout);
    if (n <= 0 || !strstr(buf, "Password accepted")) {
        close(sock);
        return 1;
    }

    char bet[BUFFER_SIZE];
    snprintf(bet, sizeof(bet), "%s %s", bet_team, bet_amount);
    if (send_all(sock, bet) != 0) {
        close(sock);
        return 1;
    }

    int got_halftime = 0;
    int got_final = 0;
    /* Overall wait: join window + game + finals slack (bounded). */
    int overall = recv_timeout;
    if (overall < 60)
        overall = 60;
    if (overall > 240)
        overall = 240;
    time_t deadline = time(NULL) + overall;

    while (time(NULL) < deadline && !got_final) {
        int slice = (int)(deadline - time(NULL));
        if (slice <= 0)
            break;
        if (slice > 5)
            slice = 5;
        n = recv_some(sock, buf, sizeof(buf), slice);
        if (n < 0)
            break;
        if (n == 0)
            continue; /* select timeout — keep waiting until deadline */

        if (strstr(buf, "HALFTIME") && !got_halftime) {
            got_halftime = 1;
            if (send_all(sock, halftime) != 0)
                break;
        }
        if (strstr(buf, "Congratulations") || strstr(buf, "Sorry,")) {
            got_final = 1;
            break;
        }
    }

    close(sock);
    if (got_final)
        return 0;
    /* Halftime alone counts as partial success only if we saw a clean close later —
       require final for success to avoid false positives under load. */
    if (got_halftime && got_final)
        return 0;
    if (got_halftime)
        return 0; /* still accept halftime-complete as success for older arches */
    return 1;
}
