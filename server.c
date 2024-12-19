#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "message.h"
#include "socket.h"

#define MAX_PLAYERS 100
#define BOARD_SIZE 3

/**
 * A structure representing a single game session of Tic-Tac-Toe.
 * Each session tracks:
 * - A unique game ID
 * - Two player file descriptors
 * - Both player names (Player X and Player O)
 * - A 3x3 board array
 * - The current turn indicator (0 for X, 1 for O)
 */
typedef struct {
    int game_id;
    int player_x_fd;
    int player_o_fd;
    char player_x_name[50];
    char player_o_name[50];
    char board[BOARD_SIZE][BOARD_SIZE];
    int current_turn; // 0 for X, 1 for O
} GameSession;

// Global counters for clients and games
static int client_count = 0;
static int game_count = 0;
static pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Append a line of text to a given file.
 *
 * \param filename The file to append to
 * \param content The line of content to write
 */
static void append_to_file(const char* filename, const char* content) {
    FILE* f = fopen(filename, "a");
    if (f) {
        fprintf(f, "%s\n", content);
        fclose(f);
    }
}

/**
 * Save the current incomplete game state (e.g., if a player quits or disconnects)
 * to a file named "saved_games.txt".
 *
 * This function records:
 * - The game ID
 * - Both player names
 * - Whose turn it was
 * - The reason for incompleteness (status)
 * - The final board state in a tic-tac-toe format
 *
 * \param game The game session to save
 * \param status A string describing why the game ended incompletely (e.g., player quit)
 */
static void save_game_state(GameSession* game, const char* status) {
    FILE* f = fopen("saved_games.txt", "a");
    if (f) {
        fprintf(f, "Game ID: %d\n", game->game_id);
        fprintf(f, "Player X: %s\n", game->player_x_name);
        fprintf(f, "Player O: %s\n", game->player_o_name);
        fprintf(f, "Current Turn: %d\n", game->current_turn);
        fprintf(f, "Status: %s\n", status);
        fprintf(f, "Final Board State:\n");
        // Print board in a tic-tac-toe style format
        fprintf(f, " %c | %c | %c\n", game->board[0][0], game->board[0][1], game->board[0][2]);
        fprintf(f, "---|---|---\n");
        fprintf(f, " %c | %c | %c\n", game->board[1][0], game->board[1][1], game->board[1][2]);
        fprintf(f, "---|---|---\n");
        fprintf(f, " %c | %c | %c\n", game->board[2][0], game->board[2][1], game->board[2][2]);
        fprintf(f, "\n------------------------\n");
        fclose(f);
    }
}

/**
 * Update player statistics after a game concludes (win, lose, or draw).
 *
 * Records game results to "player_stats.txt":
 * - Game ID
 * - If it's a draw, record both player names
 * - Otherwise, record the winner and loser
 *
 * \param game_id The ID of the completed game
 * \param player_x_name Name of Player X
 * \param player_o_name Name of Player O
 * \param winner Name of the winner if applicable, or "" if a draw
 * \param draw Non-zero if the game is a draw, 0 otherwise
 */
static void update_player_stats(int game_id, const char* player_x_name, const char* player_o_name, const char* winner, int draw) {
    FILE* f = fopen("player_stats.txt", "a");
    if (f) {
        if (draw) {
            fprintf(f, "Game #%d: Draw between %s and %s\n", game_id, player_x_name, player_o_name);
        } else {
            fprintf(f, "Game #%d: Winner: %s | Loser: %s\n", game_id, winner,
                    (strcmp(winner, player_x_name) == 0) ? player_o_name : player_x_name);
        }
        fclose(f);
    }
}

/**
 * Initialize a log file for a new game. Logs the game ID, player names,
 * and that the game has started. Each game has its own log "game_log_<id>.txt".
 *
 * \param game The game session to log
 */
static void log_game_init(GameSession* game) {
    char filename[64];
    snprintf(filename, sizeof(filename), "game_log_%d.txt", game->game_id);
    FILE* f = fopen(filename, "w");
    if (f) {
        fprintf(f, "Game ID: %d\n", game->game_id);
        fprintf(f, "Player X: %s\n", game->player_x_name);
        fprintf(f, "Player O: %s\n", game->player_o_name);
        fprintf(f, "Game Start\n");
        fclose(f);
    }
}

/**
 * Log a single move to the game's log file.
 *
 *
 * \param game The current game session
 * \param player_name The name of the player who made the move
 * \param row The row of the move (0-based internally, will add 1 for logging)
 * \param col The column of the move (0-based internally, will add 1 for logging)
 */
static void log_move(GameSession* game, const char* player_name, int row, int col) {
    char filename[64];
    snprintf(filename, sizeof(filename), "game_log_%d.txt", game->game_id);
    FILE* f = fopen(filename, "a");
    if (f) {
        fprintf(f, "%s moved to (%d, %d)\n", player_name, row+1, col+1);
        fprintf(f, "Current Board:\n");
        for (int i = 0; i < BOARD_SIZE; i++) {
            fprintf(f, " %c | %c | %c\n",
                    game->board[i][0], game->board[i][1], game->board[i][2]);
            if (i < BOARD_SIZE - 1) fprintf(f, "---|---|---\n");
        }
        fprintf(f, "\n");
        fclose(f);
    }
}

/**
 * Log the final result of the game into the "game_log_<id>.txt" file.
 *
 * \param game The game session that ended
 * \param result A string describing the game's result (winner/loser or draw)
 */
static void log_game_result(GameSession* game, const char* result) {
    char filename[64];
    snprintf(filename, sizeof(filename), "game_log_%d.txt", game->game_id);
    append_to_file(filename, result);
}

/**
 * Create a new Tic-Tac-Toe game session. This sets up:
 * - A unique game ID
 * - Assigns players X and O, their FDs and names
 * - Initializes an empty board
 * - Logs the game start
 *
 * \param player_x_fd File descriptor for Player X
 * \param player_x_name Name of Player X
 * \param player_o_fd File descriptor for Player O
 * \param player_o_name Name of Player O
 * \return A pointer to the newly created GameSession structure
 */
static GameSession* create_game(int player_x_fd, const char* player_x_name, int player_o_fd, const char* player_o_name) {
    pthread_mutex_lock(&game_mutex);
    int game_id = ++game_count; 
    pthread_mutex_unlock(&game_mutex);

    GameSession* game = malloc(sizeof(GameSession));
    game->game_id = game_id;
    game->player_x_fd = player_x_fd;
    game->player_o_fd = player_o_fd;
    strncpy(game->player_x_name, player_x_name, 50);
    strncpy(game->player_o_name, player_o_name, 50);
    game->current_turn = 0; // X always starts first

    // Initialize the 3x3 board to empty spaces
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            game->board[i][j] = ' ';
        }
    }

    log_game_init(game);
    return game;
}

/**
 * Print the current board state to the server console.
 *
 * \param game The current game session whose board we want to log
 */
static void log_board(GameSession* game) {
    printf("[Game %d] Current Board:\n", game->game_id);
    printf(" %c | %c | %c\n", game->board[0][0], game->board[0][1], game->board[0][2]);
    printf("---|---|---\n");
    printf(" %c | %c | %c\n", game->board[1][0], game->board[1][1], game->board[1][2]);
    printf("---|---|---\n");
    printf(" %c | %c | %c\n", game->board[2][0], game->board[2][1], game->board[2][2]);
    printf("\n");
}

/**
 * Check if the current board state has a winner.
 *
 * Returns 'X' if X has won, 'O' if O has won, or 0 if no winner yet.
 *
 * \param board The current 3x3 game board
 * \return 'X', 'O', or 0 depending on the game state
 */
static int check_winner(char board[BOARD_SIZE][BOARD_SIZE]) {
    // Check rows and columns
    for (int i = 0; i < BOARD_SIZE; i++) {
        // Check row i
        if (board[i][0] != ' ' && board[i][0] == board[i][1] && board[i][1] == board[i][2])
            return board[i][0];
        // Check column i
        if (board[0][i] != ' ' && board[0][i] == board[1][i] && board[1][i] == board[2][i])
            return board[0][i];
    }

    // Check diagonals
    if (board[0][0] != ' ' && board[0][0] == board[1][1] && board[1][1] == board[2][2])
        return board[0][0];
    if (board[0][2] != ' ' && board[0][2] == board[1][1] && board[1][1] == board[2][0])
        return board[0][2];

    return 0; // No winner found
}

/**
 * Send the current board state to both players over the network in a tic-tac-toe format.
 *
 * \param game The current game session
 */
static void send_board(GameSession* game) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
             "Board:\n %c | %c | %c\n---|---|---\n %c | %c | %c\n---|---|---\n %c | %c | %c\n",
             game->board[0][0], game->board[0][1], game->board[0][2],
             game->board[1][0], game->board[1][1], game->board[1][2],
             game->board[2][0], game->board[2][1], game->board[2][2]);
    send_message(game->player_x_fd, buffer);
    send_message(game->player_o_fd, buffer);
}

/**
 * Handle a single game session in a dedicated thread. This function:
 * - Coordinates turns between players
 * - Reads moves from the current player
 * - Updates the board and logs moves
 * - Checks for win or draw conditions
 * - Handles quitting or disconnection by players
 * - Ends the session when the game concludes
 *
 * \param arg A pointer to the GameSession for this game
 * \return NULL when the thread finishes
 */
static void* handle_game(void* arg) {
    GameSession* game = (GameSession*)arg;
    printf("[Game %d] Started: Player 1 (%s, X) vs Player 2 (%s, O)\n",
           game->game_id, game->player_x_name, game->player_o_name);

    int winner = 0;

    // Keep running until we have a winner or a break condition (quit, disconnect, or draw)
    while (!winner) {
        // Determine whose turn it is
        int current_player_fd = (game->current_turn == 0) ? game->player_x_fd : game->player_o_fd;
        int other_player_fd = (game->current_turn == 0) ? game->player_o_fd : game->player_x_fd;
        const char* current_player_name = (game->current_turn == 0) ? game->player_x_name : game->player_o_name;
        const char* other_player_name = (game->current_turn == 0) ? game->player_o_name : game->player_x_name;

        send_message(current_player_fd, "Your turn. Enter row and column (e.g., '1 2') or type 'quit' to exit:");
        char* move = receive_message(current_player_fd);

        if (!move) {
            // The current player disconnected abruptly
            printf("[Game %d] %s disconnected.\n", game->game_id, current_player_name);
            char status_str[100];
            snprintf(status_str, sizeof(status_str), "Incomplete - Player %s Disconnected", current_player_name);
            save_game_state(game, status_str);
            log_game_result(game, "Result: Incomplete (Disconnection)");
            send_message(other_player_fd, "Your opponent disconnected. You win by default! Game is Over.");
            break;
        }

        if (strcmp(move, "quit") == 0) {
            // Current player chose to quit the game
            printf("[Game %d] %s quit the game.\n", game->game_id, current_player_name);
            char status_str[100];
            snprintf(status_str, sizeof(status_str), "Incomplete - Player %s Quit", current_player_name);
            save_game_state(game, status_str);
            log_game_result(game, "Result: Player Quit / Incomplete");
            send_message(current_player_fd, "You quit the game. Game is Over.");
            send_message(other_player_fd, "Your opponent quit. You win! Game is Over.");
            free(move);
            break;
        }

        int row, col;
        // Parse the move as two integers
        if (sscanf(move, "%d %d", &row, &col) != 2 || row < 1 || row > BOARD_SIZE || col < 1 || col > BOARD_SIZE) {
            // Invalid input format or out-of-range move
            send_message(current_player_fd, "Invalid move. Try again.");
            free(move);
            continue;
        }

        int row_index = row - 1;
        int col_index = col - 1;

        // Check if the chosen spot is empty
        if (game->board[row_index][col_index] != ' ') {
            send_message(current_player_fd, "That spot is already taken. Try again.");
            free(move);
            continue;
        }

        // Place the 'X' or 'O' on the board
        game->board[row_index][col_index] = (game->current_turn == 0) ? 'X' : 'O';
        printf("[Game %d] %s made a move at (%d, %d)\n", game->game_id, current_player_name, row, col);

        // Log the move and update the internal structures
        log_move(game, current_player_name, row_index, col_index);
        free(move);

        log_board(game);

        // Check if we have a winner
        winner = check_winner(game->board);
        if (winner) {
            // Announce winner
            char buffer[100];
            snprintf(buffer, sizeof(buffer), "Congratulations %s! You win! Game is Over.", current_player_name);
            send_message(current_player_fd, buffer);
            snprintf(buffer, sizeof(buffer), "Sorry %s, you lost. Better luck next time! Game is Over.", other_player_name);
            send_message(other_player_fd, buffer);

            char result_line[200];
            snprintf(result_line, sizeof(result_line), "Result: %s (winner) vs %s (loser)",
                     current_player_name, other_player_name);
            log_game_result(game, result_line);

            // Update player stats with a win/loss result
            update_player_stats(game->game_id, game->player_x_name, game->player_o_name, current_player_name, 0);
            printf("[Game %d] Game is Over: %s won against %s.\n", game->game_id, current_player_name, other_player_name);
            break;
        }

        // Check for a draw (no empty spaces left and no winner)
        int draw = 1;
        for (int i = 0; i < BOARD_SIZE; i++) {
            for (int j = 0; j < BOARD_SIZE; j++) {
                if (game->board[i][j] == ' ') {
                    draw = 0;
                    break;
                }
            }
            if (!draw) break;
        }
        if (draw) {
            send_message(game->player_x_fd, "The game is a draw! Game is Over.");
            send_message(game->player_o_fd, "The game is a draw! Game is Over.");
            log_game_result(game, "Result: Draw");
            // Record the draw in player stats
            update_player_stats(game->game_id, game->player_x_name, game->player_o_name, "", 1);
            printf("[Game %d] Game is Over: The game ended in a draw.\n", game->game_id);
            break;
        }

        // Switch turns for the next iteration
        game->current_turn = 1 - game->current_turn;

        // Send the updated board to both players
        send_board(game);
    }

    // Clean up the game session
    close(game->player_x_fd);
    close(game->player_o_fd);
    free(game);
    return NULL;
}

/**
 * The main function sets up the server:
 * - Opens a server socket on an available port
 * - Listens for incoming player connections
 * - As players connect, pairs them into games
 * - If one player is waiting, the next player to connect starts a game
 * - Each game runs in its own thread
 */
int main() {
    unsigned short port = 0;
    int server_socket_fd = server_socket_open(&port);
    if (server_socket_fd == -1) {
        perror("Failed to open server socket");
        exit(EXIT_FAILURE);
    }

    // Start listening for connections(used 5 as an exmaple but can be changed)
    if (listen(server_socket_fd, 5)) {
        perror("Failed to listen on server socket");
        exit(EXIT_FAILURE);
    }

    printf("Tic-Tac-Toe Server listening on port %u\n", port);

    int waiting_player_fd = -1;
    char waiting_player_name[50];

    // Main loop: accept players and pair them for games
    while (1) {
        int client_socket_fd = server_socket_accept(server_socket_fd);
        if (client_socket_fd == -1) {
            perror("Failed to accept client connection");
            continue;
        }

        // Assign a client ID to the new player
        pthread_mutex_lock(&game_mutex);
        int client_id = ++client_count; 
        pthread_mutex_unlock(&game_mutex);

        send_message(client_socket_fd, "Welcome to Tic-Tac-Toe!\nPlease enter your name:");
        char* player_name = receive_message(client_socket_fd);
        if (!player_name) {
            // If the player failed to send their name, close and move on
            close(client_socket_fd);
            continue;
        }

        printf("[Client %d] Player %d connected as %s\n", client_id, client_id, player_name);

        // If no one is waiting, this player waits for an opponent
        if (waiting_player_fd == -1) {
            waiting_player_fd = client_socket_fd;
            strncpy(waiting_player_name, player_name, 50);
            send_message(client_socket_fd, "Waiting for an opponent...");
            free(player_name);
        } else {
            // Another player was waiting, so we can start a game
            GameSession* game = create_game(waiting_player_fd, waiting_player_name, client_socket_fd, player_name);
            free(player_name);

            // Create a thread to handle the game session
            pthread_t game_thread;
            if (pthread_create(&game_thread, NULL, handle_game, game) != 0) {
                perror("Failed to create game thread");
                free(game);
            }
            pthread_detach(game_thread);

            waiting_player_fd = -1; // Reset waiting player
        }
    }

    close(server_socket_fd);
    return 0;
}
