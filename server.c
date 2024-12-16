#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "message.h"
#include "socket.h"

#define MAX_PLAYERS 100
#define BOARD_SIZE 3

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
int client_count = 0;
int game_count = 0;
pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function to create a new game session
GameSession* create_game(int player_x_fd, const char* player_x_name, int player_o_fd, const char* player_o_name) {
    pthread_mutex_lock(&game_mutex);
    int game_id = ++game_count;
    pthread_mutex_unlock(&game_mutex);

    GameSession* game = malloc(sizeof(GameSession));
    game->game_id = game_id;
    game->player_x_fd = player_x_fd;
    game->player_o_fd = player_o_fd;
    strncpy(game->player_x_name, player_x_name, 50);
    strncpy(game->player_o_name, player_o_name, 50);
    game->current_turn = 0; // X starts

    // Initialize the board
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            game->board[i][j] = ' ';
        }
    }
    return game;
}

// Function to print the board to the server log
void log_board(GameSession* game) {
    printf("[Game %d] Current Board:\n", game->game_id);
    printf(" %c | %c | %c\n", game->board[0][0], game->board[0][1], game->board[0][2]);
    printf("---|---|---\n");
    printf(" %c | %c | %c\n", game->board[1][0], game->board[1][1], game->board[1][2]);
    printf("---|---|---\n");
    printf(" %c | %c | %c\n", game->board[2][0], game->board[2][1], game->board[2][2]);
    printf("\n");
}

// Check if the game has a winner or is a draw
int check_winner(char board[BOARD_SIZE][BOARD_SIZE]) {
    for (int i = 0; i < BOARD_SIZE; i++) {
        if (board[i][0] != ' ' && board[i][0] == board[i][1] && board[i][1] == board[i][2]) return board[i][0];
        if (board[0][i] != ' ' && board[0][i] == board[1][i] && board[1][i] == board[2][i]) return board[0][i];
    }
    if (board[0][0] != ' ' && board[0][0] == board[1][1] && board[1][1] == board[2][2]) return board[0][0];
    if (board[0][2] != ' ' && board[0][2] == board[1][1] && board[1][1] == board[2][0]) return board[0][2];
    return 0; // No winner yet
}

// Send the current game board to both players
void send_board(GameSession* game) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "Board:\n %c | %c | %c\n---|---|---\n %c | %c | %c\n---|---|---\n %c | %c | %c\n",
             game->board[0][0], game->board[0][1], game->board[0][2],
             game->board[1][0], game->board[1][1], game->board[1][2],
             game->board[2][0], game->board[2][1], game->board[2][2]);
    send_message(game->player_x_fd, buffer);
    send_message(game->player_o_fd, buffer);
}

// Handle a single game session
void* handle_game(void* arg) {
    GameSession* game = (GameSession*)arg;
    printf("[Game %d] Started: Player 1 (%s, X) vs Player 2 (%s, O)\n", game->game_id, game->player_x_name, game->player_o_name);

    int winner = 0;

    while (!winner) {
        int current_player_fd = (game->current_turn == 0) ? game->player_x_fd : game->player_o_fd;
        int other_player_fd = (game->current_turn == 0) ? game->player_o_fd : game->player_x_fd;
        const char* current_player_name = (game->current_turn == 0) ? game->player_x_name : game->player_o_name;
        const char* other_player_name = (game->current_turn == 0) ? game->player_o_name : game->player_x_name;

        send_message(current_player_fd, "Your turn. Enter row and column (e.g., '1 2') or type 'quit' to exit:");
        char* move = receive_message(current_player_fd);

        if (!move || strcmp(move, "quit") == 0) {
            printf("[Game %d] %s quit the game.\n", game->game_id, current_player_name);
            send_message(current_player_fd, "You quit the game. Game is Over.");
            send_message(other_player_fd, "Your opponent quit. You win! Game is Over.");
            printf("[Game %d] Game is Over: %s quit.\n", game->game_id, current_player_name);
            free(move);
            break;
        }

        int row, col;
        if (sscanf(move, "%d %d", &row, &col) != 2 || row < 0 || row >= BOARD_SIZE || col < 0 || col >= BOARD_SIZE || game->board[row][col] != ' ') {
            send_message(current_player_fd, "Invalid move. Try again.");
            free(move);
            continue;
        }

        free(move);

        // Update the board
        game->board[row][col] = (game->current_turn == 0) ? 'X' : 'O';
        printf("[Game %d] %s made a move at (%d, %d)\n", game->game_id, current_player_name, row, col);
        log_board(game);

        // Check for a winner
        winner = check_winner(game->board);
        if (winner) {
            char buffer[100];
            snprintf(buffer, sizeof(buffer), "Congratulations %s! You win! Game is Over.", current_player_name);
            send_message(current_player_fd, buffer);
            snprintf(buffer, sizeof(buffer), "Sorry %s, you lost. Better luck next time! Game is Over.", other_player_name);
            send_message(other_player_fd, buffer);
            printf("[Game %d] Game is Over: %s won against %s.\n", game->game_id, current_player_name, other_player_name);
            break;
        }

        // Check for a draw
        int draw = 1;
        for (int i = 0; i < BOARD_SIZE; i++) {
            for (int j = 0; j < BOARD_SIZE; j++) {
                if (game->board[i][j] == ' ') draw = 0;
            }
        }
        if (draw) {
            send_message(game->player_x_fd, "The game is a draw! Game is Over.");
            send_message(game->player_o_fd, "The game is a draw! Game is Over.");
            printf("[Game %d] Game is Over: The game ended in a draw.\n", game->game_id);
            break;
        }

        // Switch turns
        game->current_turn = 1 - game->current_turn;

        // Send the updated board
        send_board(game);
    }

    close(game->player_x_fd);
    close(game->player_o_fd);
    free(game);
    return NULL;
}

// Main server logic
int main() {
    unsigned short port = 0;
    int server_socket_fd = server_socket_open(&port);
    if (server_socket_fd == -1) {
        perror("Failed to open server socket");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket_fd, 5)) {
        perror("Failed to listen on server socket");
        exit(EXIT_FAILURE);
    }

    printf("Tic-Tac-Toe Server listening on port %u\n", port);

    int waiting_player_fd = -1;
    char waiting_player_name[50];

    while (1) {
        int client_socket_fd = server_socket_accept(server_socket_fd);
        if (client_socket_fd == -1) {
            perror("Failed to accept client connection");
            continue;
        }

        pthread_mutex_lock(&game_mutex);
        int client_id = ++client_count;
        pthread_mutex_unlock(&game_mutex);

        send_message(client_socket_fd, "Welcome to Tic-Tac-Toe!\nPlease enter your name:");
        char* player_name = receive_message(client_socket_fd);
        if (!player_name) {
            close(client_socket_fd);
            continue;
        }

        printf("[Client %d] Player %d connected as %s\n", client_id, client_id, player_name);

        if (waiting_player_fd == -1) {
            waiting_player_fd = client_socket_fd;
            strncpy(waiting_player_name, player_name, 50);
            send_message(client_socket_fd, "Waiting for an opponent...");
            free(player_name);
        } else {
            GameSession* game = create_game(waiting_player_fd, waiting_player_name, client_socket_fd, player_name);
            free(player_name);

            pthread_t game_thread;
            if (pthread_create(&game_thread, NULL, handle_game, game) != 0) {
                perror("Failed to create game thread");
                free(game);
            }
            pthread_detach(game_thread);

            waiting_player_fd = -1;
        }
    }

    close(server_socket_fd);
    return 0;
}
