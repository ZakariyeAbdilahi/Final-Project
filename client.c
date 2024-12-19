#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>


#include "message.h"
#include "socket.h"

/**
 * Runs in a separate thread on the client side.
 * It continuously waits for messages from the server and prints them.
 * If the server connection closes or the server sends a "Game is Over" message,
 * this thread stops and closes the socket.
 *
 * \param arg A pointer to the socket file descriptor
 * \return NULL when the thread finishes
 */
void* receive_messages(void* arg) {
    int socket_fd = *(int*)arg;

    while (1) {
        // Wait for a message from the server
        char* message = receive_message(socket_fd);
        if (!message) {
            // If no message is received, the server likely disconnected
            printf("Game is Over.\n");
            break;
        }

        // Print the received message
        printf("%s\n", message);

        // Check if the message indicates the game has ended
        if (strstr(message, "Game is Over") != NULL) {
            free(message);
            break;
        }

        // Free the message after use
        free(message);
    }

    // Close the socket and exit the thread when done
    close(socket_fd);
    exit(EXIT_SUCCESS);
}

/**
 * The main function for the client:
 * 1. Connects to the server using the given hostname and port
 * 2. Receives a welcome message and sends the player's name
 * 3. Creates a receiving thread to listen for server messages continuously
 * 4. The main thread handles user input for moves (or quitting)
 *
 *
 * \param argc Argument count
 * \param argv Argument vector (argv[1] = server_name, argv[2] = port)
 * \return 0 on successful completion
 */
int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server name> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Parse server name and port from command line arguments
    char* server_name = argv[1];
    unsigned short port = (unsigned short)atoi(argv[2]);

    // Attempt to connect to the server
    int socket_fd = socket_connect(server_name, port);
    if (socket_fd == -1) {
        perror("Failed to connect to server");
        exit(EXIT_FAILURE);
    }

    printf("Successfully Connection Established\n");

    // Receive the initial welcome message and instructions from the server
    char* welcome_message = receive_message(socket_fd);
    if (welcome_message) {
        printf("%s\n", welcome_message);
        free(welcome_message);
    }

    // Prompt the user for their name and send it to the server
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        perror("Error reading name");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }
    // Remove newline from the input
    buffer[strcspn(buffer, "\n")] = '\0';
    send_message(socket_fd, buffer);

    // Create a separate thread to handle incoming messages from the server
    pthread_t receive_thread;
    if (pthread_create(&receive_thread, NULL, receive_messages, &socket_fd) != 0) {
        perror("Failed to create receive thread");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    // The main thread now handles user inputs for moves.
    // Players type moves like "1 2" or "quit" to exit.
    while (1) {
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) break;

        // Remove trailing newline
        buffer[strcspn(buffer, "\n")] = '\0';

        // Send the player’s input (move or command) to the server
        if (send_message(socket_fd, buffer) == -1) {
            perror("Failed to send message");
            break;
        }

        // If the player types "quit", print a confirmation and break out of loop
        if (strcmp(buffer, "quit") == 0) {
            printf("You quit the game. Game is Over.\n");
            break;
        }
    }

    // Close the socket and end the client program
    close(socket_fd);
    return 0;
}
