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

// ─── Constants ───────────────────────────────────────────────────────────────
#define TEAM_NAME_MAX_LENGTH  100
#define PORT                  8084
#define MULTICAST_PORT        8085
#define MULTICAST_GROUP       "239.0.0.1"
#define BUFFER_SIZE           1024
#define MAX_CLIENTS           10
#define GAME_DURATION         10          // Seconds until the game starts
#define GAME_LENGTH           30          // Seconds (total game simulation time)
#define SECRET_PASSWORD       "1234"
#define AUTH_TIMEOUT_SEC      15
#define BET_TIMEOUT_SEC       15
#define HalfTimer_respose     10          // Seconds to wait for halftime response
#define MAX_GAMES             5

#define waiting   0
#define in_prog   1
#define comptited 2

// ─── Structs ─────────────────────────────────────────────────────────────────
typedef struct {
    int socket;
    struct sockaddr_in address;
    int client_id;
    int bet_team;
    int bet_amount;
    int bet_received;
    int recive_halftime;        // Flag: halftime response received
    char comments[BUFFER_SIZE];
    int connected;
    time_t last_keep_alive;
} Client;

typedef struct {
    char message;
} Packet;

typedef struct {
    int score[2];
    int current_minute;
    int game_running;
    int halftime;
    char group1[BUFFER_SIZE];
    char group2[BUFFER_SIZE];
} GameState;

typedef struct {
    int game_id;
    GameState game_state;
    Client *clients[MAX_CLIENTS];
    int client_count;
    time_t start_time;
    pthread_mutex_t game_lock;
    int status;   // waiting / in_prog / comptited
} GameSession;

// ─── Globals (defined in globals.c) ──────────────────────────────────────────
extern pthread_mutex_t lock;
extern time_t          start_time;

extern Client    *clients[MAX_CLIENTS];
extern GameState  game_state;
extern int        client_count;
extern int        udp_multicast_socket;
extern struct sockaddr_in multicast_addr;

extern int game_over;
extern int server_fd_global;
extern int DebugMode;
extern int print_keep_alive_prints;

extern GameSession    *all_games[MAX_GAMES];
extern pthread_mutex_t lobby_lock;

extern const char *countries[];
#define NUM_COUNTRIES 10

// ─── Test Flags (defined in globals.c) ───────────────────────────────────────
extern int test_drop_halftime;
extern int test_multicast_to_wrong_reciver;
extern int test_keepalive;
extern int disable_keep_alive_check;
extern int test_drop_final_message;
extern int test_drop_password;
extern int test_drop_place_bet;

// ─── Function Prototypes ──────────────────────────────────────────────────────

// network.c
void  accept_bets(int server_fd);
void  setup_udp_multicast(void);
void  broadcast_game_update(const char *message);
void  broadcast_half_time_message(void);
void *broadcast_remaining_time(void *arg);
void  start_game(void);
void  close_all_client_sockets(void);
void  notify_clients_of_interruption(void);
void  handle_signal(int sig);
void  log_client_message(Client *client, const char *message);

// client_handler.c
void *handle_new_client(void *arg);
void *handle_client_requests(void *arg);
void  send_final_message(Client *client, int wrong_message);

// game.c
void *simulate_game(void *arg);
void  shuffle_countries(char *shuffled[], int n);
void  assign_teams(GameState *gs);
void  format_game_update(char *update, size_t buf_size, const GameState *gs);
/* void *check_keep_alive(void *arg); // currently disabled */

#endif // SERVER_H
