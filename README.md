# SocketGambleGame
SocketGambleGame is a multiplayer server-client application where users place bets on simulated matches. It uses TCP for client communication and UDP multicast for game updates, showcasing socket programming and basic betting logic in a networked environment.
## Requirements
- **C Compiler**: GCC or any other C compiler
- **Linux**: Tested on Linux, should work on other Unix-like systems
- **POSIX Threads**: For handling multiple clients concurrently

## Server Script
The server script is responsible for managing the game simulation, handling client connections, processing bets, and broadcasting game updates.
1) *TCP and UDP Sockets:*
 The server uses TCP sockets to handle reliable communication with clients, including authentication, bet placement, and sending important game-related messages.
   UDP multicast is used to broadcast real-time game updates to all connected clients, providing a lightweight and efficient way to share information.
2) *Client Management:* The server maintains a list of connected clients using a dynamically allocated array of Client structs, each containing details about the client's socket, betting status, and other metadata.Clients are handled in separate threads, allowing multiple clients to interact with the server concurrently without blocking.
3) *Game Simulation:*  The server simulates a game between two randomly assigned teams, broadcasting the score updates and current game minute via UDP multicast.
    At halftime, the server sends a special message asking clients if they want to double their bets. It waits for client responses and handles defaulting to 'NO' if no response is received.
  
4) *Keep-Alive Mechanism:* A dedicated thread monitors the keep-alive messages from each client to ensure they are still connected. If a client misses keep-alive messages, the server disconnects them.
5) *Error Handling and Testing:* The server includes several test flags (`test_drop_halftime`, `test_multicast_to_wrong_receiver`) to simulate errors and test the robustness of the protocol.
The server can also handle signals (SIGINT, SIGTSTP) to gracefully close connections and shut down.

6) *Final Results:* After the game concludes, the server sends final results to each client based on their bets, including whether they won or lost.
## Client Script
The client script connects to the server, allows the user to authenticate, place bets, and receive real-time game updates. It also handles responses to server prompts during the game.

## Client Script


1) *TCP and UDP Sockets:* Uses TCP for reliable communication with the server (authentication, betting, game results) and UDP multicast to receive real-time game updates like score changes.

2) *Game Interaction:* Authenticates with the server using a password, places bets on the game, and can choose to double the bet during halftime based on server prompts.

3) **Keep-Alive Mechanism:** Periodically sends keep-alive messages to the server to maintain the connection; includes a `test_keepalive_not_recived` flag for testing keep-alive failure scenarios.

4) *Error Handling:* Manages missed or unexpected messages, allowing the client to request missing information, such as halftime prompts, and handles server interruptions gracefully.

5) *Signal Handling:* Responds to interruptions (e.g., Ctrl+C or Ctrl+Z) by notifying the server and safely closing connections to release resources properly.

6) *Final Result Processing:* Receives and verifies final result messages from the server to ensure they match the client's bet, requesting the correct message if necessary.






