
#define _DEFAULT_SOURCE
#include <stdio.h>A
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
#include <netinet/in.h>
#include <signal.h>
#include <ctype.h>

#define MAX_PASSWORD_LENGTH 1018  
#define PORT 8084
#define MULTICAST_PORT 8085
#define MULTICAST_GROUP "239.0.0.1"
#define BUFFER_SIZE 1024
#define GAME_LENGTH 4
#define  TEAM_NAME_MAX_LENGTH 100

//varibles declaretion
int recived_bet;
int udp_multicast_socket;
int tcp_socket;
int bet_amount = 0;
struct sockaddr_in multicast_addr;
struct ip_mreqn mreq;
int DebugMode=0;
int ready_to_receive_updates = 0; // Flag to indicate readiness
int stop_udp_listener = 0; // Flag to stop the UDP listener thread
int halftime_received = 0; // Flag to check if halftime message was received
int current_minute = 0;  // Track the current minute of the game
char buffer[BUFFER_SIZE] = {0};
int show_keep_alive_print=0;
int test_keepalive_not_recived=0; //DONE
char team1_name[TEAM_NAME_MAX_LENGTH];
char team2_name[TEAM_NAME_MAX_LENGTH];
void trim_whitespace(char *str);

//functions decleration
void* listen_for_updates(void* arg);
void setup_udp_multicast();
int setup_tcp_connection();
int authenticate_with_server(int sock);
int place_bet(int sock);
void handle_signal(int signal);
void handle_interruption();
void* send_keep_alive(void* arg); 
void extract_team_names(const char* message) ;
int process_server_messages(int tcp_socket, pthread_t update_thread) ;

typedef struct {
    int team;  // 0 for tie, 1 for team1, 2 for team2
    int amount;
    char my_group[100];
} Bet;

Bet my_bet;

int main() {
    char buffer[BUFFER_SIZE] = {0};

    // Set up signal handler for Ctrl+C and Ctrl+Z
    signal(SIGINT, handle_signal);  // Handle Ctrl+C
    signal(SIGTSTP, handle_signal); // Handle Ctrl+Z

    // Setup UDP multicast for receiving game updates
    setup_udp_multicast();
    printf("UDP multicast setup completed.\n");

    // Setup TCP connection
    tcp_socket = setup_tcp_connection();
    if (tcp_socket < 0) {
        return -1;
    }
    printf("Connected to server successfully.\n");

    // Start thread to listen for UDP multicast updates
    pthread_t update_thread;
    pthread_create(&update_thread, NULL, listen_for_updates, NULL);

    // Start the keep-alive thread
    pthread_t keep_alive_thread;
    pthread_create(&keep_alive_thread, NULL, send_keep_alive, NULL);

    // Authenticate with server
    if (authenticate_with_server(tcp_socket) < 0) {
        close(tcp_socket);
        printf("Socket closed due to authentication failure.\n");
        stop_udp_listener = 1; // Signal the UDP thread to stop
        pthread_join(update_thread, NULL);
        return 1;
    }

    // Place bet
    if (place_bet(tcp_socket) < 0) {
        close(tcp_socket);
        printf("Socket closed after failed bet placement.\n");
        stop_udp_listener = 1; // Signal the UDP thread to stop
        pthread_join(update_thread, NULL);
        return 1;
    }

    // Now ready to receive updates
    ready_to_receive_updates = 1;
    process_server_messages(tcp_socket, update_thread);
    
    return 0;
    
}

void setup_udp_multicast() {
    // Create a UDP socket
    if ((udp_multicast_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("UDP socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set reuse options
    int reuse = 1;
    if (setsockopt(udp_multicast_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse)) < 0) {
        perror("Setting SO_REUSEADDR failed");
        close(udp_multicast_socket);
        exit(EXIT_FAILURE);
    }

    // Bind the socket to the multicast port
    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    multicast_addr.sin_port = htons(MULTICAST_PORT);

    if (bind(udp_multicast_socket, (struct sockaddr*)&multicast_addr, sizeof(multicast_addr)) < 0) {
        perror("UDP socket bind failed");
        close(udp_multicast_socket);
        exit(EXIT_FAILURE);
    }

    // Join the multicast group
    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP);
    mreq.imr_address.s_addr = htonl(INADDR_ANY);
    mreq.imr_ifindex = 0;

    if (setsockopt(udp_multicast_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0) {
        perror("Setting IP_ADD_MEMBERSHIP failed");
        close(udp_multicast_socket);
        exit(EXIT_FAILURE);
    }
}

void* listen_for_updates(void* arg) {
    char buffer[BUFFER_SIZE];
    fd_set readfds;
    struct timeval tv;
    int retval;

    while (!stop_udp_listener) {
        FD_ZERO(&readfds);
        FD_SET(udp_multicast_socket, &readfds);

        // Set timeout for select
        tv.tv_sec = 1;  // Timeout of 1 second
        tv.tv_usec = 0;

        retval = select(udp_multicast_socket + 1, &readfds, NULL, NULL, &tv);

        if (retval == -1) {
            perror("select()");
            break;
        } 
        else if (retval > 0) {
            if (FD_ISSET(udp_multicast_socket, &readfds)) {
                int bytes_received = recv(udp_multicast_socket, buffer, BUFFER_SIZE, 0);
                if (bytes_received < 0) {
                    if (stop_udp_listener) break; // Exit if stop flag is set
                    perror("UDP recv failed");
                    break;
                } else if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    if (ready_to_receive_updates) { // Process updates only if ready
                        if (strstr(buffer, "interrupted")) {
                            handle_interruption();  // Handle interruption if found
                        } 
                        else if (strstr(buffer, "HALFTIME")) {
                            printf("[ERROR:] ITS SENT FROM UDP SOCKET AND NOT A TCP\n");
                            halftime_received = 1;  // Mark halftime as received
                        } 
                        else if (strstr(buffer, "Minute")){
                            if (DebugMode){printf("[DURUNG THEGAME UPDATE]");}
                            printf("%s",buffer);
                            sscanf(buffer, "Minute %d:", &current_minute);
                            if (current_minute == GAME_LENGTH / 2) {
                                sleep(1);
                                if (!halftime_received) {
                                    printf("Halftime message not received, requesting it from the server...\n");
                                    char request[BUFFER_SIZE] = "REQUEST_HALFTIME_MESSAGE";
                                    send(tcp_socket, request, strlen(request), 0);
                                }
                            }  
                            if(current_minute==GAME_LENGTH)   {
                                //TO-DO IMPLEMENT CONGRATS/SOORY MESSGE DROPPING
                            }                     
                        }
                        else if(strstr(buffer, "remaining")){
                            if (DebugMode){printf("[REMAINIG:]");}
                            printf("%s",buffer);
                        }
                        else if(strstr(buffer, "Congratulations!") || strstr(buffer, "Sorry")) {
                            printf("[ERROR: ITS SENT FROM UDP SOCKET AND NOT A TCP\n");
                        }
                        else{
                            printf("[UDP] %s",buffer);


                        }

                    }
                }
            }
        }
    }

    // If halftime message not received, request it from the server
    if (!halftime_received && ready_to_receive_updates) {
        char request[BUFFER_SIZE] = "REQUEST_HALFTIME_MESSAGE";
        send(tcp_socket, request, strlen(request), 0);
        printf("Requested halftime message from server.\n");
    }

    close(udp_multicast_socket);
    printf("Multicast socket closed after update thread finished.\n");
    return NULL;
}

int setup_tcp_connection() {
    int sock;
    struct sockaddr_in serv_addr;

    // Create TCP socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\nSocket creation error\n");
        return -1;
    }
    printf("Socket created successfully.\n");

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported\n");
        return -1;
    }
    printf("Address converted successfully.\n");

    // Connect to server via TCP
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed\n");
        return -1;
    }

    return sock;
}

int authenticate_with_server(int sock) {
    char buffer[BUFFER_SIZE];
    int bytes_read;

    // Receive initial message from server
    bytes_read = read(sock, buffer, BUFFER_SIZE);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        // Extract the team names from the received message
        extract_team_names(buffer);

        if (strstr(buffer, "interrupted")) {
            handle_interruption();  // Handle interruption if found
        }
        if(strstr(buffer, "Welcome")){
            if(DebugMode){printf("[authenticate:]");}
            printf("%s",buffer);
        }
        else{
            printf("[CKECK THIS MESSAGE, ITS NOT DIFEINED IN THE CLIENT!!]: %s",buffer);
        }
       
    } else {
        perror("read");
        return -1;
    }

    // Send password to the server with AUTH: prefix
    //char password[MAX_PASSWORD_LENGTH + 1];  // Ensure space for null terminator
    struct timeval tv;
    fd_set readfds;
    char password[1019];
    // Set the timeout value
    tv.tv_sec = 15;
    tv.tv_usec = 0;

    // Set the file descriptor for stdin
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    fflush(stdout);  // Ensure the prompt is displayed immediately
    int result = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
    if (result > 0) {
        scanf("%s", password);
        password[strcspn(password, "\n")] = 0;  // Remove trailing newline

        char auth_message[BUFFER_SIZE];

        // Calculate maximum password length based on buffer size and prefix length
        size_t max_password_length = BUFFER_SIZE - strlen("AUTH:") - 1;
        if (strlen(password) > max_password_length) {
            password[max_password_length] = '\0'; // Truncate the password if necessary
        }

        // Format the auth_message safely
        snprintf(auth_message, BUFFER_SIZE, "AUTH:%s", password);
        printf("Sending password to server: %s\n", auth_message);  // Debug print
        send(sock, auth_message, strlen(auth_message), 0);
    } else {
        printf("\nTimeout: No input received within 15 seconds.\n");
        return -1;
    }

    // Wait for response on password
    bytes_read = read(sock, buffer, BUFFER_SIZE);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        if (strstr(buffer, "interrupted")) {
            handle_interruption();  // Handle interruption if found
        }
        if (DebugMode){printf("[XXX]:");}
        printf("%s", buffer);  // This should print "Password accepted. Place your bet..."
        if (strstr(buffer, "Password accepted") == NULL) {
            return -1; // Password was not accepted
        }
    } else {
        perror("read");
        return -1;
    }

    return 0;
}

int place_bet(int sock) {
    char buffer[BUFFER_SIZE];
    struct timeval tv;
    fd_set readfds;

    // Set the timeout value for placing a bet
    tv.tv_sec = 15;
    tv.tv_usec = 0;

    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    // Prompt the user for the bet
    fflush(stdout);  // Ensure the prompt is displayed immediately

    int result = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
    if (result > 0) {
        scanf("%d %d", &my_bet.team, &my_bet.amount); // Store bet in my_bet structure

        // Store the correct team name based on the bet
        if (my_bet.team == 1) {
            strncpy(my_bet.my_group, team1_name, TEAM_NAME_MAX_LENGTH);  // Use the actual team name for Team 1
        } else if (my_bet.team == 2) {
            strncpy(my_bet.my_group, team2_name, TEAM_NAME_MAX_LENGTH);  // Use the actual team name for Team 2
        } else {
            strncpy(my_bet.my_group, "tie", TEAM_NAME_MAX_LENGTH);
        }

        // Send the bet details to the server
        snprintf(buffer, BUFFER_SIZE, "%d %d", my_bet.team, my_bet.amount);
        send(sock, buffer, strlen(buffer), 0);

        // Print the bet details including the team name
        printf("Bet details sent to server: Team %d, Amount %d (the name of the group is %s)\n", 
               my_bet.team, my_bet.amount, my_bet.my_group);
    } else {
        printf("\nTimeout: No bet placed within 15 seconds.\n");
        return -1;
    }

    return 0;
}

void handle_signal(int signal) {
    if (signal == SIGINT || signal == SIGTSTP) {  // Handle Ctrl+C or Ctrl+Z
        printf("\nYou are paying %d\n", bet_amount);

        // Notify the server about the client's termination
        char buffer[BUFFER_SIZE];
        snprintf(buffer, BUFFER_SIZE, "CLIENT_TERMINATED %d", bet_amount);
        send(tcp_socket, buffer, strlen(buffer), 0);

        // Close sockets and exit
        stop_udp_listener = 1;  // Signal the UDP thread to stop
        close(tcp_socket);       // Close the TCP socket
        close(udp_multicast_socket); // Close the UDP socket
        exit(0);                 // Exit the program
    }
}

void handle_interruption() {
    printf("\nThe game has been interrupted by the server. Closing sockets and exiting...\n");
    // Close sockets and exit
    close(tcp_socket);       // Close the TCP socket
    close(udp_multicast_socket); // Close the UDP socket
    exit(0);                 // Exit the program
}

void* send_keep_alive(void* arg) {
    while (1) {
        if (!test_keepalive_not_recived) {
            if (send(tcp_socket, "KEEP_ALIVE:", strlen("KEEP_ALIVE:"), 0) < 0) {
                perror("Keep-alive send failed, server might have disconnected");
                break;
            }
            if(show_keep_alive_print){printf("Sent keep-alive to server.\n");}  // Debug print
        } else {
            printf("Keep-alive not sent due to test_keepalive_not_recived flag.\n");  // Debug print
            break;
        }
        sleep(5); // Send keep-alive every 5 seconds
    }
    
    // Close the TCP socket if the server disconnects
    close(tcp_socket);
    printf("TCP socket closed after keep-alive failure.\n");

    // Close the UDP socket
    close(udp_multicast_socket);
    printf("UDP multicast socket closed.\n");

    printf("Exiting client due to server disconnection...\n");
    exit(1); // Exit the program
    return NULL;
}

void extract_team_names(const char* message) {
    sscanf(message, "Welcome! The game between %99s vs %99s", team1_name, team2_name);
    
    // Remove any trailing punctuation or special characters (like ",")
    strtok(team1_name, ",");
    strtok(team2_name, ",");
    
 //   printf("Extracted team names: Team 1: %s, Team 2: %s\n", team1_name, team2_name);  // Debug print
}

int process_server_messages(int tcp_socket, pthread_t update_thread) {
    char buffer[BUFFER_SIZE];
    char received_group[TEAM_NAME_MAX_LENGTH];

    while (1) {
        int bytes_read = read(tcp_socket, buffer, BUFFER_SIZE);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            if (strstr(buffer, "interrupted")) {
                handle_interruption();  // Handle interruption if found
            } 
            else if (strstr(buffer, "HALFTIME")) {
                halftime_received = 1;  // Mark halftime as received
                char response[BUFFER_SIZE];

                // Prompt the user to double their bet
                printf("Do you want to double your bet? (YES/NO): ");
                fflush(stdout);  // Ensure the prompt is printed immediately
                scanf("%s", response);
                // Clear the input buffer before reading new input
                //fgets(response, BUFFER_SIZE, stdin);
              //  response[strcspn(response, "\n")] = 0;  // Remove trailing newline
              //  printf("%s",response);
                // Ensure the response is either YES or NO
                if (strcmp(response, "YES") != 0 && strcmp(response, "NO") != 0) {
                    printf("client sent:%s",response);
                    printf("Invalid response. Defaulting to 'NO'.\n");
                    strcpy(response, "NO");
                }

                // Send the response to the server
                printf("Sending response to server: %s\n", response); // Debug print
                send(tcp_socket, response, strlen(response), 0); // Send the response via TCP
            }
            else if (strstr(buffer, "Congratulations!") || strstr(buffer, "Sorry")) {
             //   trim_whitespace(received_group);
           //   trim_whitespace(my_bet.my_group);
                // Extract the group name from the received message
                if (sscanf(buffer, "Congratulations! You won your bet of %*d $ on %99s", received_group) == 1 ||
                    sscanf(buffer, "Sorry, you lost your bet of %*d $ on %99s", received_group) == 1) {
                    // Validate if the message matches the bet
                    if (strcmp(my_bet.my_group, received_group)==0) 
                    {
                       // printf( "%s,%s",team1_name,received_group);
                        if(DebugMode){("Final message received:");}
                        printf("%s", buffer);
                        if(DebugMode){printf("Message matches the bet. Closing socket.\n");}
                        break;  // Exit the loop and close the socket
                    } else {
                       // printf("[my group is:]%s\n",my_bet.my_group);
                        //printf("[RECIVEGROUP IS:]%s\n",received_group);
                 //     printf("[TEST THE INCOME]:%s\n",buffer);
                        printf("Received incorrect final message. Requesting the correct message.\n");
                        char request[BUFFER_SIZE] = "REQUEST_FINAL_MESSAGE";
                        send(tcp_socket, request, strlen(request), 0);
                    }
                } 
                else {
                    printf("Could not extract group name from message: %s\n", buffer);
                }
            }            
            
            else {
                printf("[TCP]%s", buffer);  // Print the game update or result

                // Check if the message contains "Congratulations!" or "Sorry"
                if (strstr(buffer, "Congratulations!") || strstr(buffer, "Sorry")) {
                    printf("Final message received. Closing socket.\n");
                    break;  // Exit the loop and close the socket
                }
            }
        } else if (bytes_read == 0) {
            // Connection closed by server
            break;
        } else {
            perror("read");
            break;
        }
    }

    stop_udp_listener = 1; // Signal the UDP thread to stop
    pthread_join(update_thread, NULL); // Wait for the thread to finish
    close(tcp_socket); // Close the TCP socket
    printf("Socket closed after receiving updates.\n");
    return 0;
}