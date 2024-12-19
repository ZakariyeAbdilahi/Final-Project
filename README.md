# Tic-Tac-Toe Multiplayer Networked Game

## Overview
This project implements a networked, multiplayer Tic-Tac-Toe game using TCP/IP sockets. The game server coordinates multiple matches, each running in its own thread, enabling concurrent gameplay between players. Players connect to the server from separate client programs, input their names, and either wait for an opponent or start playing immediately if one is already waiting.

Key features include:  
- Real-time interaction between two remote players  
- Multiple concurrent games running on the server  
- Clear instructions and board state updates sent to each player after every move  
- Support for quitting mid-game (with the other player winning by default)  
- Detection and announcement of wins, losses, and draws  
- Logging of each game to a dedicated file (moves, outcomes)  
- Storing incomplete games (if a player quits or disconnects) and final board states in `saved_games.txt`  
- Maintaining player statistics (including wins, losses, and draws) in `player_stats.txt`

## Architecture
The system consists of two main components:

1. **Server**:
   - Listens for incoming connections on a specified port.
   - Pairs up connected players and starts a dedicated game session in a new thread.
   - Manages game logic: validating moves, updating the board, detecting game results.
   - Saves incomplete games and logs each completed or incomplete game’s state.
   - Records player stats after each game.

2. **Client**:
   - Connects to the server using a hostname and port.
   - Prompts the user for their name and sends it to the server.
   - Receives real-time board updates and instructions from the server.
   - Allows the user to enter moves (e.g., `1 2` for row 1, column 2) or type `quit` to end the game.
   - Runs a separate thread to continuously receive server messages while the main thread handles user input.

## Files
- **server.c**: The server implementation.
- **client.c**: The client implementation.
- **message.h/.c**: Message handling functions for sending and receiving data over sockets.
- **socket.h**: Socket helper functions for setting up server and client connections.
- **player_stats.txt**: Generated at runtime, logs outcomes of completed games.
- **saved_games.txt**: Generated at runtime, stores states of incomplete (quit or disconnected) games.
- **game_log_\<id\>.txt**: Generated at runtime for each game, detailing moves and final results.

## Building
To build the project, ensure you have a C compiler and make sure the provided `Makefile` (if any) is configured correctly.

Run:
```bash
make
```

This should produce `server` and `client` executables.

## Running the Server
Run the server on a machine:
```bash
./server
```
The server will print the port it is listening on. For example:
```
Tic-Tac-Toe Server listening on port 12345
```

## Running the Client
On the same machine or a different one, run the client and specify the server address and port:
```bash
./client <server_address> <port>
```
For example, if the server is running on `localhost` and listening on port `12345`:
```bash
./client localhost 12345
```

You will see a prompt for your name and then for moves once an opponent joins.

## Gameplay Instructions
1. **Name Input**: After connecting, enter your name when prompted.
2. **Waiting/Opponent Found**: If no opponent is available, you will wait. Otherwise, the game starts immediately, and you’ll be assigned either Player X or O.
3. **Making a Move**: Type row and column coordinates (1-based) like `1 2` to mark the top-middle cell.  
4. **Quiting**: Type `quit` at any time to exit the match (the opponent wins by default).
5. **Winning, Losing, Drawing**: The server detects wins, losses, or draws and notifies both players. Once the game ends, the server logs it.

## Logging and Stats
- Each completed game generates a `game_log_<id>.txt` file detailing moves and results.
- Incomplete games are recorded in `saved_games.txt` along with the final board state and the reason for incompleteness.
- Every finished game updates `player_stats.txt` to reflect winners, losers, and draws.

## Acknowledgments
- [Zakariye Abdilahi & Jonathan]
- [Charlie Curtsinger]: For providing the starter code for server.c and client.c(Taken from CSC213: Operating Systems & Parallel Algorithms, Networking Excercise)
