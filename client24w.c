#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// Server IPs for serverw24, mirror1, and mirror2
const char *serverIPs[] = {"127.0.0.1", "192.168.1.101", "192.168.1.102"};
const char *serverNames[] = {"serverw24", "mirror1", "mirror2"};
#define SERVER_PORT 9230
#define BUFFER_SIZE 1024
#define NUM_SERVERS 3

int main() {
    int clientSocket;
    struct sockaddr_in serverAddr;
    char buffer[BUFFER_SIZE];

    for (int i = 0; i < NUM_SERVERS; ++i) {
        // Create a socket
        clientSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (clientSocket < 0) {
            perror("Socket creation failed");
            continue;
        }

        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(SERVER_PORT);
        serverAddr.sin_addr.s_addr = inet_addr(serverIPs[i]);

        // Attempt to connect to the server
        if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            perror("Connect failed");
            close(clientSocket);
            continue;  // Try next server
        }

        printf("Connected to %s at %s.\n", serverNames[i], serverIPs[i]);

        // Client interaction loop
        while (1) {
            printf("Enter a command: ");
            fgets(buffer, BUFFER_SIZE, stdin);
            buffer[strcspn(buffer, "\n")] = 0; // Remove newline char

            if (strcmp(buffer, "quit") == 0) {
                send(clientSocket, buffer, strlen(buffer), 0);
                break;
            }

            send(clientSocket, buffer, strlen(buffer), 0);
            int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
            if (bytesReceived > 0) {
                printf("Server response: %s\n", buffer);
            } else {
                printf("Server disconnected or error receiving data.\n");
                break;
            }
        }

        close(clientSocket);
        printf("Disconnected from %s.\n", serverNames[i]);

        if (i < NUM_SERVERS - 1) {
            printf("Attempting to connect to the next server...\n");
        }
    }

    return 0;
}
