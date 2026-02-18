#include "client.h"

// ─── TCP Connection ───────────────────────────────────────────────────────────

int setup_tcp_connection(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Socket creation error\n");
        return -1;
    }
    printf("TCP socket created.\n");

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(PORT)
    };

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("Invalid address\n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Connection failed\n");
        return -1;
    }

    return sock;
}

// ─── Parse Team Names from Welcome Message ────────────────────────────────────

void extract_team_names(const char *message) {
    sscanf(message, "Welcome! The game between %99s vs %99s", team1_name, team2_name);
    strtok(team1_name, ",");
    strtok(team2_name, ",");
}

// ─── Authentication ───────────────────────────────────────────────────────────

int authenticate_with_server(int sock) {
    char buf[BUFFER_SIZE];

    // ─── Receive welcome message ──────────────────────────────────────────────
    int bytes = read(sock, buf, BUFFER_SIZE);
    if (bytes <= 0) { perror("read"); return -1; }
    buf[bytes] = '\0';

    if (strstr(buf, "interrupted")) {
        handle_interruption();
    } else if (strstr(buf, "The game has already started")) {
        printf("[GameStarted] %s", buf);
        return -1;
    } else {
        int remaining_time;
        if (sscanf(buf, "WELCOME_DATA:%99[^:]:%99[^:]:%d",
                   team1_name, team2_name, &remaining_time) == 3) {
            printf("Welcome! The game between %s vs %s starts in %d seconds.\n",
                   team1_name, team2_name, remaining_time);
        } else {
            printf("[UNDEFINED MESSAGE]: %s", buf);
        }
    }

    // ─── Send password ────────────────────────────────────────────────────────
    fd_set readfds;
    struct timeval tv = { .tv_sec = 15, .tv_usec = 0 };
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    fflush(stdout);

    if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) <= 0) {
        printf("Timeout: no password entered within 15 seconds.\n");
        return -1;
    }

    char password[1019];
    scanf("%s", password);
    password[strcspn(password, "\n")] = '\0';

    char auth_msg[BUFFER_SIZE];
    size_t max_pw = BUFFER_SIZE - strlen("AUTH:") - 1;
    if (strlen(password) > max_pw)
        password[max_pw] = '\0';

    snprintf(auth_msg, BUFFER_SIZE, "AUTH:%s", password);
    printf("Sending: %s\n", auth_msg);
    send(sock, auth_msg, strlen(auth_msg), 0);

    // ─── Receive password response ────────────────────────────────────────────
    bytes = read(sock, buf, BUFFER_SIZE);
    if (bytes <= 0) { perror("read"); return -1; }
    buf[bytes] = '\0';

    if (strstr(buf, "interrupted"))
        handle_interruption();

    if (DebugMode) printf("[AUTH RESPONSE] ");
    printf("%s", buf);

    if (!strstr(buf, "Password accepted"))
        return -1;

    return 0;
}

// ─── Place Bet ────────────────────────────────────────────────────────────────

int place_bet(int sock) {
    fd_set readfds;
    struct timeval tv = { .tv_sec = 15, .tv_usec = 0 };
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    fflush(stdout);

    if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) <= 0) {
        printf("Timeout: no bet placed within 15 seconds.\n");
        return -1;
    }

    scanf("%d %d", &my_bet.team, &my_bet.amount);

    if      (my_bet.team == 1) strncpy(my_bet.my_group, team1_name, TEAM_NAME_MAX_LENGTH);
    else if (my_bet.team == 2) strncpy(my_bet.my_group, team2_name, TEAM_NAME_MAX_LENGTH);
    else                       strncpy(my_bet.my_group, "tie",      TEAM_NAME_MAX_LENGTH);

    char buf[BUFFER_SIZE];
    snprintf(buf, BUFFER_SIZE, "%d %d", my_bet.team, my_bet.amount);
    send(sock, buf, strlen(buf), 0);

    printf("Bet sent: Team: %s, Amount: %d $\n", my_bet.my_group, my_bet.amount);
    return 0;
}