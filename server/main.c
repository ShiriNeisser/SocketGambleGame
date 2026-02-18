//Server Script

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
#include "types.h"
#include "network.c"
#include "game.h"
#include "tests.h"
pthread_mutex_t lock;
time_t start_time;



Client *clients[MAX_CLIENTS];
GameState game_state;
int print_keep_alive_prints=0;
int client_count = 0;
int udp_multicast_socket;
struct sockaddr_in multicast_addr;
int DebugMode=0;
GameSession* all_games[MAX_GAMES];      // The new "list" of all games
pthread_mutex_t lobby_lock;           // The new global lock for the game list



// --- Function Prototypes ---
void accept_bets(int server_fd);
void handle_signal(int signal);
void notify_clients_of_interruption();
void close_all_client_sockets();
void broadcast_half_time_message();
void log_client_message(Client *client, const char *message);
void* check_keep_alive(void* arg);
void* handle_client_requests(void* arg);
void* handle_new_client(void* arg);
void send_final_message(Client *client, int wrong_message);
void* broadcast_remaining_time(void* arg);



//



void* handle_client_requests(void* arg) {
    Client *client = (Client *)arg;
    char buffer[BUFFER_SIZE];

    while (1) {
        int bytes_received = recv(client->socket, buffer, BUFFER_SIZE, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            printf("Received '%s' from client %d\n", buffer, client->client_id);

            // Handle keep-alive messages
            if (strncmp(buffer, "KEEP_ALIVE:", 11) == 0) {
                pthread_mutex_lock(&lock);
                client->last_keep_alive = time(NULL);
                pthread_mutex_unlock(&lock);
                if(print_keep_alive_prints){
                printf("Keep-alive received from client %d.\n", client->client_id);
                }
            } else if (strstr(buffer, "CLIENT_TERMINATED")) {
                printf("Client %d has terminated the connection. Decreasing client count.\n", client->client_id);
                pthread_mutex_lock(&lock);
                client_count--;
                pthread_mutex_unlock(&lock);
                close(client->socket);
                client->connected = 0;
                return NULL;
            } else if (strstr(buffer, "REQUEST_HALFTIME_MESSAGE")) {
                // Resend the halftime message if requested
                pthread_mutex_lock(&lock);
                char half_time_message[BUFFER_SIZE];
                snprintf(half_time_message, BUFFER_SIZE, "HALFTIME: Do you want to double your bet? Reply with 'YES' or 'NO'.\n");
                send(client->socket, half_time_message, strlen(half_time_message), 0);
                pthread_mutex_unlock(&lock);
            } else if (strstr(buffer, "YES") || strstr(buffer, "NO")) {
                // Record the client's decision to double the bet or not
                pthread_mutex_lock(&lock);
                client->recive_halftime = 1; // Mark that the client has responded
                if (strstr(buffer, "YES")) {
                    client->bet_amount *= 2; // Double the bet
                }
                pthread_mutex_unlock(&lock);
                printf("Client %d chose to %s their bet.\n", client->client_id, strstr(buffer, "YES") ? "double" : "not double");
            } 
            else if (strncmp(buffer, "REQUEST_FINAL_MESSAGE", 21) == 0) {
                printf("Client %d requested the correct final message.\n", client->client_id);
                send_final_message(client, 0); // Send the correct final message
            } else if (sscanf(buffer, "%d %d", &client->bet_team, &client->bet_amount) == 2) 
            {
                // Classify the bet message from the client
                printf("Client %d placed a bet on team %d with amount %d.\n", client->client_id, client->bet_team, client->bet_amount);
                // Additional processing or logging can be done here

            }
            else {
                printf("[THIS MESSEGE ISNT CLASSIFIED BY THE SERVER!!!!!!!! ]client : %d sent : %s\n", client->client_id, buffer);
            }
        } else if (bytes_received == 0) {
            // Connection closed by client
            close(client->socket);
            printf("Client %d disconnected. Socket closed.\n", client->client_id);
            break;
        } else {
            // Error occurred
            perror("recv");
            close(client->socket);
            printf("Client %d disconnected due to error. Socket closed.\n", client->client_id);
            break;
        }
    }

    return NULL;
}

void* handle_new_client(void* arg) {
    Client *client = (Client *)arg;
    char buffer[BUFFER_SIZE];

    int remaining_time = GAME_DURATION - difftime(time(NULL), start_time);

    // Format the welcome message with team names and remaining time
    int ret = snprintf(buffer, BUFFER_SIZE,
    "WELCOME_DATA:%s:%s:%d",
    game_state.group1, game_state.group2, remaining_time);


    if (ret >= BUFFER_SIZE) {
        // Handle truncation, perhaps by notifying the client
    //    snprintf(buffer, BUFFER_SIZE, "Welcome! The game is about to start. Please enter the secret password: ");
    }

    if (!test_drop_password) {
        send(client->socket, buffer, strlen(buffer), 0);
    } else {
        printf("Simulating dropped password prompt for client %d.\n", client->client_id);
    }


    while (1) {
        // Use select to set a 15-second timeout for receiving the password
        struct timeval tv;
        fd_set readfds;
        tv.tv_sec = 15;
        tv.tv_usec = 0;
        FD_ZERO(&readfds);
        FD_SET(client->socket, &readfds);

        int result = select(client->socket + 1, &readfds, NULL, NULL, &tv);
        if (result > 0) {
            // Receive message from client
            int bytes_read = recv(client->socket, buffer, BUFFER_SIZE, 0);
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                if(! strncmp(buffer, "KEEP_ALIVE:", 11) == 0){

                printf("Received message from client %d: %s\n", client->client_id, buffer);  // Debug print
                }
                // Check if the message is a keep-alive message
                if (strncmp(buffer, "KEEP_ALIVE:", 11) == 0) {
                    // Ignore the keep-alive message and continue waiting for the password
                    printf("Ignored KEEP_ALIVE message from client %d.\n", client->client_id);
                    continue;
                }

                // Check if the message is a password (AUTH) message
                if (strncmp(buffer, "AUTH:", 5) == 0) {
                    char* received_password = buffer + 5; // Skip the "AUTH:" prefix
                    char *expected_password = "1234";  // password
                    if (strcmp(received_password, expected_password) == 0) {
                        ret = snprintf(buffer, BUFFER_SIZE, "Password accepted. Place your bet (0): tie, (1): %s, (2): %s) and amount (BY DOLLARS): ",  game_state.group1, game_state.group2);
                    if (ret >= BUFFER_SIZE) {
                         snprintf(buffer, BUFFER_SIZE, "Password accepted. Place your bet (0: tie, 1: Team 1, 2: Team 2) and amount: "); 
                    }
                        send(client->socket, buffer, strlen(buffer), 0);

                        // Set a timeout for receiving the bet
                        tv.tv_sec = 15;
                        tv.tv_usec = 0;
                        FD_ZERO(&readfds);
                        FD_SET(client->socket, &readfds);

                        result = select(client->socket + 1, &readfds, NULL, NULL, &tv);
                        if (result > 0) {
                            // Receive bet from client
                            bytes_read = recv(client->socket, buffer, BUFFER_SIZE, 0);
                            if (bytes_read > 0) {
                                buffer[bytes_read] = '\0';
                                sscanf(buffer, "%d %d", &client->bet_team, &client->bet_amount);
                                client->connected = 1;
                                log_client_message(client, buffer);
                                client->bet_received = 1;  // Mark that the bet has been received

                                pthread_t request_thread;
                                pthread_create(&request_thread, NULL, handle_client_requests, (void *)client);
                                pthread_detach(request_thread);
                            }
                        } else {
                            printf("Client %d did not place a bet within 15 seconds. Disconnecting.\n", client->client_id);
                            snprintf(buffer, BUFFER_SIZE, "Timeout: No bet placed within 15 seconds. Connection closed.\n");
                            send(client->socket, buffer, strlen(buffer), 0);
                            close(client->socket);
                            printf("Client %d socket closed due to timeout.\n", client->client_id);
                        }
                    } else {
                        snprintf(buffer, BUFFER_SIZE, "Incorrect password. Connection closed.\n");
                        send(client->socket, buffer, strlen(buffer), 0);
                        close(client->socket);
                        printf("Client %d socket closed due to incorrect password.\n", client->client_id);
                    }
                    break; // Exit the loop after processing the password
                } else {
                    printf("Unexpected message from client during authentication: %s\n", buffer);
                }
            }
        } else {
            printf("Client %d did not provide a password within 15 seconds. Disconnecting.\n", client->client_id);
            snprintf(buffer, BUFFER_SIZE, "Timeout: No input received within 15 seconds. Connection closed.\n");
            send(client->socket, buffer, strlen(buffer), 0);
            close(client->socket);
            printf("Client %d socket closed due to timeout.\n", client->client_id);
            break;
        }
    }

    return NULL;
}

void* broadcast_remaining_time(void* arg) {
    char buffer[BUFFER_SIZE];

    while (1) {
        pthread_mutex_lock(&lock);
        int remaining_time = GAME_DURATION - difftime(time(NULL), start_time);

        pthread_mutex_unlock(&lock);
        if (remaining_time <= 0) {
            remaining_time = 0;
        }

        snprintf(buffer, BUFFER_SIZE, "Time remaining until the game starts: %d seconds\n", remaining_time);
        broadcast_game_update(buffer);
        printf("Broadcasted remaining time until the game starts: %d seconds\n", remaining_time);

        if (remaining_time <= 0 && !game_state.game_running) {
            start_game();
            break;
        }

        sleep(1); // Broadcast every second
    }

    return NULL;
}



void accept_bets(int server_fd) {
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int new_socket;

    start_time = time(NULL);

    // Assign teams before clients connect
    assign_teams(&game_state);  // Assign team names before any client joins

    pthread_t broadcast_thread;
    pthread_create(&broadcast_thread, NULL, broadcast_remaining_time, NULL);

    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);

        if (new_socket >= 0) {
            pthread_mutex_lock(&lock);
            if (game_state.game_running) {
                const char *message = "The game has already started. You cannot join now.\n";
                send(new_socket, message, strlen(message), 0);
                close(new_socket);
                printf("Client attempted to join after game started. Socket closed.\n");
                pthread_mutex_unlock(&lock);
                continue;
            }

            Client *client = (Client *)malloc(sizeof(Client));
            client->socket = new_socket;
            client->address = address;
            client->client_id = client_count++;
            client->bet_received = 0;
            client->recive_halftime = 0;
            memset(client->comments, 0, BUFFER_SIZE);
            client->last_keep_alive = time(NULL); // Initialize keep-alive timestamp
            printf("Client %d logged in\n", client->client_id);
            clients[client_count - 1] = client;
            pthread_mutex_unlock(&lock);

            pthread_t client_thread;
            pthread_create(&client_thread, NULL, handle_new_client, (void *)client);
            pthread_detach(client_thread);
        }
    }

    pthread_join(broadcast_thread, NULL);
}



void log_client_message(Client *client, const char *message) {
    snprintf(client->comments, BUFFER_SIZE, "Client %d sent: %s", client->client_id, message);
    printf("%s\n", client->comments);
}

void handle_signal(int signal) {
    if (signal == SIGINT || signal == SIGTSTP) {  // Handle Ctrl+C or Ctrl+Z
        printf("\nThe game has been interrupted by the server.\n");

        // Notify all clients that the game has been interrupted
        notify_clients_of_interruption();

        // Close all client sockets and exit
        close_all_client_sockets();
        close(udp_multicast_socket); // Close the UDP socket
        pthread_exit(NULL);
    }
}

void notify_clients_of_interruption() {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "The game has been interrupted by the server.\n");
    for (int i = 0; i < client_count; i++) {
        send(clients[i]->socket, buffer, strlen(buffer), 0);
        close(clients[i]->socket);
    }
    printf("All clients have been notified of the interruption.\n");
}

void close_all_client_sockets() {
    for (int i = 0; i < client_count; i++) {
        close(clients[i]->socket);
        printf("Closed socket for client %d.\n", clients[i]->client_id);
    }
}

void broadcast_half_time_message() {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "HALFTIME: Do you want to double your bet? Reply with 'YES' or 'NO'.\n");

    pthread_mutex_lock(&lock);
    for (int i = 0; i < client_count; i++) {

        if (clients[i]->connected) {
            if(test_drop_halftime){
                printf("Simulating dropped message for client %d.\n", clients[i]->client_id);
                continue; // Skip sending the message                
            }
                send(clients[i]->socket, buffer, strlen(buffer), 0); // Send the half-time message via TCP
                printf("Sent halftime message to client %d.\n", clients[i]->client_id);
        }
    }
    pthread_mutex_unlock(&lock);

    printf("Broadcasted halftime message to all clients.\n");

    // Set the halftime flag in GameState
    pthread_mutex_lock(&lock);
    game_state.halftime = 1;
    pthread_mutex_unlock(&lock);
}




void send_final_message(Client *client, int wrong_message) {
    /*
    wrong_message is a flag that if its true: send weong message to a client
    */
    char result[BUFFER_SIZE];
    const char *correct_group = (client->bet_team == 0) ? "tie" : (client->bet_team == 1) ? game_state.group1 : game_state.group2;

    if (client->connected == 1) {
        printf("Preparing to send final message to client %d.\n", client->client_id);

        if (wrong_message) {
            // Create a temporary client structure with a different bet_team
            Client temp_client = *client; // Copy the original client
            temp_client.bet_team = (client->bet_team + 1) % 3; // Change bet_team to simulate wrong message (0 -> 1, 1 -> 2, 2 -> 0)

            // Adjust the correct group for the temporary client
            const char *wrong_group = (temp_client.bet_team == 0) ? "tie" : (temp_client.bet_team == 1) ? game_state.group1 : game_state.group2;

            // Generate a message intended for the temporary client
            if ((temp_client.bet_team == 1 && game_state.score[0] > game_state.score[1]) ||
                (temp_client.bet_team == 2 && game_state.score[1] > game_state.score[0]) ||
                (temp_client.bet_team == 0 && game_state.score[0] == game_state.score[1])) {
                snprintf(result, BUFFER_SIZE, "Congratulations! You won your bet of %d $ on %s\n",
                         temp_client.bet_amount, wrong_group);
            } else {
                snprintf(result, BUFFER_SIZE, "Sorry, you lost your bet of %d $ on %s\n",
                         temp_client.bet_amount, wrong_group);
            }

            printf("Simulating wrong message sent to client %d (Message intended for team %d).\n", client->client_id, temp_client.bet_team);
        } 
        else {
            // Generate the correct message for the original client
            if ((client->bet_team == 1 && game_state.score[0] > game_state.score[1]) ||
                (client->bet_team == 2 && game_state.score[1] > game_state.score[0]) ||
                (client->bet_team == 0 && game_state.score[0] == game_state.score[1])) {
                snprintf(result, BUFFER_SIZE, "Congratulations! You won your bet of %d $ on %s\n",
                         client->bet_amount, correct_group);
            } else {
                snprintf(result, BUFFER_SIZE, "Sorry, you lost your bet of %d $ on %s\n",
                         client->bet_amount, correct_group);
            }

            printf("Sent correct final message to client %d., %s\n", client->client_id,result);
        }

        // Send the generated message to the client
        send(client->socket, result, strlen(result), 0);
        // DO NOT CLOSE THE SOCKET HERE.
        // The handle_client_requests thread is still listening.
        // The client will close the connection, which that thread will detect.
        printf("Sent final message to client %d.\n", client->client_id);

        // Close the client socket after sending the message
        sleep(1); // Ensure the message is sent before closing the socket
        close(client->socket);
        printf("Client %d disconnected. Socket closed.\n", client->client_id);
    }
}



int main() {
    srand(time(NULL)); // Seed for random number generator
    GameState game_state;

    signal(SIGINT, handle_signal);  // Handle Ctrl+C
    signal(SIGTSTP, handle_signal); // Handle Ctrl+Z


    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    pthread_mutex_init(&lock, NULL);

    // TCP socket creation
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    printf("TCP socket created successfully.\n");

    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("Socket options set successfully.\n");

    // Bind the TCP socket
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("Socket bound successfully.\n");

    // Setup UDP multicast
    setup_udp_multicast();
    printf("UDP multicast setup completed.\n");

    // Start listening for incoming TCP connections
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("Server is listening on port %d.\n", PORT);

    // Start the keep-alive check thread
    
    /*pthread_t keep_alive_thread;
    pthread_create(&keep_alive_thread, NULL, check_keep_alive, NULL);
    */

    accept_bets(server_fd);

    close_all_client_sockets();

    pthread_exit(NULL);
    return 0;
}
