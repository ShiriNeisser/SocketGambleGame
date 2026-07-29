/*server.h*/
#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <time.h>
#include <signal.h>
#include <stdint.h>

#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_EVENTS 128

/* ─── Constants ─────────────────────────────────────────────────────────────── */
#define TEAM_NAME_MAX_LENGTH  100
#define PORT                  8084
#define MULTICAST_PORT        8085
#define MULTICAST_GROUP       "239.0.0.1"
#define BUFFER_SIZE           1024
#define MAX_CLIENTS           1024
#define LISTEN_BACKLOG        512
#define GAME_DURATION         30          /* Seconds until the game starts */
#define GAME_LENGTH           30          /* Seconds (total game simulation time) */
#define SECRET_PASSWORD       "1234"
#define AUTH_TIMEOUT_SEC      15
#define BET_TIMEOUT_SEC       15
#define HalfTimer_respose     10          /* Seconds to wait for halftime response */
#define MAX_GAMES             5

#define waiting   0
#define in_prog   1
#define comptited 2

/* ─── Enums ─────────────────────────────────────────────────────────────────── */
typedef enum {
    GAME_PHASE_PRE,
    GAME_PHASE_FIRST_HALF,
    GAME_PHASE_HALFTIME,
    GAME_PHASE_SECOND_HALF,
    GAME_PHASE_OVER
} GamePhase;

typedef enum {
    CLIENT_WAIT_AUTH,
    CLIENT_WAIT_BET,
    CLIENT_IN_GAME,
    CLIENT_DISCONNECTED
} ClientState;

/* ─── Forward declarations ──────────────────────────────────────────────────── */
typedef struct ServerContext ServerContext;

/* ─── Structs ───────────────────────────────────────────────────────────────── */
typedef struct {
    int socket;
    struct sockaddr_in address;
    int client_id;
    int bet_team;
    int bet_amount;
    int bet_received;
    int recive_halftime;
    char comments[BUFFER_SIZE];
    int connected;
    time_t last_keep_alive;
    ServerContext *ctx;
    ClientState state;
} Client;

typedef struct {
    int score[2];
    int current_minute;
    int game_running;
    int halftime;
    char group1[TEAM_NAME_MAX_LENGTH];
    char group2[TEAM_NAME_MAX_LENGTH];
    GamePhase phase;
} GameState;

typedef struct {
    int game_id;
    GameState game_state;
    Client *clients[MAX_CLIENTS];
    int client_count;
    time_t start_time;
    pthread_mutex_t game_lock;
    int status;
} GameSession;

struct ServerContext {
    GameState game_state;
    time_t start_time;
};

/* ─── Globals (defined in globals.c) ────────────────────────────────────────── */
extern pthread_mutex_t lock;

extern Client    *clients[MAX_CLIENTS];
extern int        client_count;

extern int game_over;
extern int socket_fd_global;
extern int DebugMode;

#define NUM_COUNTRIES 10

/* ─── Test Flags (defined in globals.c) ─────────────────────────────────────── */
extern int test_drop_halftime;
extern int test_multicast_to_wrong_reciver;
extern int test_keepalive;
extern int disable_keep_alive_check;
extern int test_drop_final_message;
extern int test_drop_password;
extern int test_drop_place_bet;

/* ─── Function Prototypes ────────────────────────────────────────────────────── */

void  accept_bets(int socket_fd, ServerContext *ctx);
void  setup_udp_multicast(void);
void  broadcast_game_update(const char *message);
void  broadcast_half_time_message(ServerContext *ctx);
void *broadcast_remaining_time(void *arg);
void  start_game(ServerContext *ctx);
void  close_all_client_sockets(void);
void  notify_clients_of_interruption(void);
void  handle_signal(int sig);
void  log_client_message(Client *client, const char *message);

void  send_final_message(Client *client, int wrong_message);

void *simulate_game(void *arg);
void  shuffle_countries(char *shuffled[], int n);
void  assign_teams(GameState *gs);
void  format_game_update(char *update, size_t buf_size, const GameState *gs);

Client *find_client_by_socket(int socket);

void handle_new_connection(int socket_fd, int epoll_fd, ServerContext *ctx);
void handle_client_event(int client_fd, int epoll_fd);

void handle_auth_message(Client *client, char *buffer);
void handle_bet_message(Client *client, char *buffer);
void handle_game_message(Client *client, char *buffer);

void make_socket_nonblocking(int fd);

#endif /* SERVER_H */
