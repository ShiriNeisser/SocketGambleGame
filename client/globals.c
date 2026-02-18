#include "client.h"

int                recived_bet              = 0;
int                udp_multicast_socket     = 0;
int                tcp_socket               = 0;
int                bet_amount               = 0;
struct sockaddr_in multicast_addr;
struct ip_mreqn    mreq;

int                DebugMode                = 0;
int                ready_to_receive_updates = 0;
int                stop_udp_listener        = 0;
int                halftime_received        = 0;
int                current_minute           = 0;
char               buffer[BUFFER_SIZE]      = {0};
int                show_keep_alive_print    = 0;
int                test_keepalive_not_recived = 0; // DONE

char               team1_name[TEAM_NAME_MAX_LENGTH];
char               team2_name[TEAM_NAME_MAX_LENGTH];
int                GAME_LENGTH              = 0;

Bet                my_bet;