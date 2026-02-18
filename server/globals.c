#include "server.h"

// ─── Mutex & Timing ───────────────────────────────────────────────────────────
pthread_mutex_t lock;
time_t          start_time;

// ─── Clients & Game State ─────────────────────────────────────────────────────
Client    *clients[MAX_CLIENTS];
GameState  game_state;
int        client_count = 0;

// ─── Network ──────────────────────────────────────────────────────────────────
int                udp_multicast_socket;
struct sockaddr_in multicast_addr;

// ─── Flags ────────────────────────────────────────────────────────────────────
int DebugMode               = 0;
int print_keep_alive_prints = 0;

// ─── Multi-game Support ───────────────────────────────────────────────────────
GameSession    *all_games[MAX_GAMES];
pthread_mutex_t lobby_lock;

// ─── Countries List ───────────────────────────────────────────────────────────
const char *countries[] = {
    "Brazil", "Germany", "Argentina", "France",    "Spain",
    "Italy",  "England", "Netherlands", "Portugal", "Belgium"
};

// ─── Test Flags ───────────────────────────────────────────────────────────────
int test_drop_halftime              = 0;  // DONE
int test_multicast_to_wrong_reciver = 0;  // DONE
int test_keepalive                  = 0;  // Implemented in client
int disable_keep_alive_check        = 0;
int test_drop_final_message         = 0;  // TO-DO
int test_drop_password              = 0;  // TO-DO
int test_drop_place_bet             = 0;  // TO-DO

int game_over        = 0;
int server_fd_global = 0;
