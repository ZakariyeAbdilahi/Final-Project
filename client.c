#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "message.h"
#include "socket.h"

// Function to receive messages from the server
void* receive_messages(void* arg) {
    int socket_fd = *(int*)arg;

    while (1) {
        char* message = receive_message(socket_fd);
        if (!message) {
            printf("Game is Over.\n");
            break;
        }

        printf("%s\n", message);

        // Check if the game is over
        if (strstr(message, "Game is Over") != NULL) {
            free(message);
            break;
        }

        free(message);
    }

    close(socket_fd);
    exit(EXIT_SUCCESS);
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server name> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char* server_name = argv[1];
    unsigned short port = atoi(argv[2]);

    int socket_fd = socket_connect(server_name, port);
    if (socket_fd == -1) {
        perror("Failed to connect to server");
        exit(EXIT_FAILURE);
    }

    printf("Successfully Connection Established\n");

    // Receive the welcome message and prompt for name
    char* welcome_message = receive_message(socket_fd);
    if (welcome_message) {
        printf("%s\n", welcome_message);
        free(welcome_message);
    }

    // Send player's name to the server
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        perror("Error reading name");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }
    buffer[strcspn(buffer, "\n")] = '\0'; // Remove newline
    send_message(socket_fd, buffer);

    // Create a thread to receive messages from the server
    pthread_t receive_thread;
    if (pthread_create(&receive_thread, NULL, receive_messages, &socket_fd) != 0) {
        perror("Failed to create receive thread");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    // Main thread handles user input for moves
    while (1) {
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) break;

        buffer[strcspn(buffer, "\n")] = '\0'; // Remove newline
        if (send_message(socket_fd, buffer) == -1) {
            perror("Failed to send message");
            break;
        }

        // Exit input loop if player types "quit"
        if (strcmp(buffer, "quit") == 0) {
            printf("You quit the game. Game is Over.\n");
            break;
        }
    }

    close(socket_fd);
    return 0;
}
