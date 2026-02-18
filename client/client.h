#ifndef CLIENT_H
#define CLIENT_H

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <ctype.h>

// ─── Constants ───────────────────────────────────────────────────────────────
#define MAX_PASSWORD_LENGTH  1018
#define PORT                 8084
#define MULTICAST_PORT       8085
#define MULTICAST_GROUP      "239.0.0.1"
#define BUFFER_SIZE          1024
#define TEAM_NAME_MAX_LENGTH 100

// ─── Bet Struct ───────────────────────────────────────────────────────────────
typedef struct {
    int  team;          // 0=tie, 1=team1, 2=team2
    int  amount;
    char my_group[TEAM_NAME_MAX_LENGTH];
} Bet;

// ─── Globals (defined in client_globals.c) ───────────────────────────────────
extern int                 recived_bet;
extern int                 udp_multicast_socket;
extern int                 tcp_socket;
extern int                 bet_amount;
extern struct sockaddr_in  multicast_addr;
extern struct ip_mreqn     mreq;
extern int                 DebugMode;
extern int                 ready_to_receive_updates;
extern int                 stop_udp_listener;
extern int                 halftime_received;
extern int                 current_minute;
extern char                buffer[BUFFER_SIZE];
extern int                 show_keep_alive_print;
extern int                 test_keepalive_not_recived;
extern char                team1_name[TEAM_NAME_MAX_LENGTH];
extern char                team2_name[TEAM_NAME_MAX_LENGTH];
extern int                 GAME_LENGTH;
extern Bet                 my_bet;

// ─── Function Prototypes ──────────────────────────────────────────────────────

// udp.c
void  setup_udp_multicast(void);
void *listen_for_updates(void *arg);

// tcp.c
int   setup_tcp_connection(void);
int   authenticate_with_server(int sock);
int   place_bet(int sock);
void  extract_team_names(const char *message);

// messaging.c
int   process_server_messages(int tcp_socket, pthread_t update_thread);
void  handle_signal(int sig);
void  handle_interruption(void);
/* void *send_keep_alive(void *arg); // currently disabled */

#endif // CLIENT_H