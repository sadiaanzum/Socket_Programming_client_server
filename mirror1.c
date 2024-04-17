//Submitted By : Navjot Singh & Sadia Anzum
//Student ID : 110125931 & 110126278

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>
#include <limits.h>
#include <fcntl.h>

#define PORT 9231
#define BUFFER_SIZE 1024
#define MAX_DIRS 512

typedef struct {
    char *name;
    time_t mtime; // Modification time used as a fallback
} Directory;


void performDirectoryListingAlphabetically(int client_socket)
{
    printf("Current directory: %s\n", getenv("HOME"));

    FILE *fp;
    char result[BUFFER_SIZE] = "";
    char command[] = "find ~ -maxdepth 1 -type d -printf '%f\n' | sort";

    // Execute the command
    fp = popen(command, "r");
    if (fp == NULL)
    {
        perror("Error executing command");
        exit(EXIT_FAILURE);
    }

    // Read the output of the command
    while (fgets(result, sizeof(result), fp) != NULL)
    {
        // Send the directory name to the client
        send(client_socket, result, strlen(result), 0);
    }

    // Close the file pointer
    pclose(fp);
}

void performDirectoryListingByCreationTime(int client_socket) {

    printf("Current directory: %s\n", getenv("HOME"));
    
    FILE *fp;
    char result[BUFFER_SIZE] = "";
    char command[1024]; // Buffer for command string

    // Construct the command to list directories sorted by creation time
    snprintf(command, sizeof(command), "find ~ -maxdepth 1 -type d -printf '%%T+\\t%%p\\n' | sort");

    // Execute the command
    fp = popen(command, "r");
    if (fp == NULL) {
        perror("Error executing command");
        exit(EXIT_FAILURE);
    }

    // Read the output of the command and send to client
    while (fgets(result, sizeof(result), fp) != NULL) {
        send(client_socket, result, strlen(result), 0);
    }

    // Close the file pointer
    pclose(fp);
}

void sendFileInformation(const char *filename, const char *path, int clientSocket) {
    char filePath[PATH_MAX];
    snprintf(filePath, sizeof(filePath), "%s/%s", path, filename);
    struct stat statbuf;
    char response[BUFFER_SIZE];

    if (stat(filePath, &statbuf) < 0) {
        snprintf(response, BUFFER_SIZE, "File not found\n");
        send(clientSocket, response, strlen(response), 0);
        return;
    }

    if (S_ISREG(statbuf.st_mode)) {
        snprintf(response, BUFFER_SIZE, "File: %s\nSize: %ld bytes\nCreated: %sPermissions: %o\n",
                 filename, statbuf.st_size, ctime(&statbuf.st_ctime), statbuf.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));
        send(clientSocket, response, strlen(response), 0);
    } else {
        snprintf(response, BUFFER_SIZE, "It is not a regular file\n");
        send(clientSocket, response, strlen(response), 0);
    }

}

void sendFilesInRange(long size1, long size2, const char *path, int clientSocket) {
    if (size1 > size2 || size1 < 0 || size2 < 0) {
        send(clientSocket, "Invalid size range", 18, 0);
        return;
    }

    char tarFileName[] = "temp.tar.gz";
    char command[1024];
    printf("sending tar");

    // Construct the find command to select files within the specified size range and archive them
    snprintf(command, sizeof(command),
             "find '%s' -type f -size +%ldc -size -%ldc -exec tar -czf '%s' {} +",
             path, size1, size2, tarFileName);

    // Execute the command
    if (system(command) != 0) {
        perror("Failed to execute find or tar");
        send(clientSocket, "Failed to create tar file\n", 26, 0);
        return;
    }

    // Open the tar file for sending
    int tar_fd = open(tarFileName, O_RDONLY);
    if (tar_fd == -1) {
        perror("No File Found");
        send(clientSocket, "No File Found\n", 24, 0);
        return;
    }

    // Check the size of the tar file
    struct stat stat_buf;
    if (fstat(tar_fd, &stat_buf) == -1 || stat_buf.st_size == 0) {
        if (stat_buf.st_size == 0) {
            send(clientSocket, "No files found in specified range\n", 34, 0);
        } else {
            send(clientSocket, "Failed to get tar file size\n", 28, 0);
        }
        close(tar_fd);
        //remove(tarFileName);
        return;
    }

    // Send the tar file size first
    send(clientSocket, &stat_buf.st_size, sizeof(stat_buf.st_size), 0);

    // Transmit the tar file
    off_t offset = 0;
    ssize_t bytes_sent;
    
    while (offset < stat_buf.st_size) {
        bytes_sent = sendfile(clientSocket, tar_fd, &offset, stat_buf.st_size - offset);
        if (bytes_sent <= 0) {
            perror("Failed to send tar file");
            break;
        }
    }
    
    close(tar_fd);
    send(clientSocket, "Tar file Created\n", strlen("Tar File Created\n"), 0);
    
    //remove(tarFileName);

    if (bytes_sent > 0) {
        send(clientSocket, "File sent successfully", 21, 0);


    } else {
        send(clientSocket, "Error sending file", 18, 0);
    }
}

void sendFilesByExtension(const char *extensions, const char *path, int clientSocket) {
    char response[BUFFER_SIZE] = {0};
    DIR *dir;
    struct dirent *entry;
    char filePath[PATH_MAX];
    char tarFileName[] = "temp.tar.gz";

    // Create a temporary tar file
    char tarCmd[512];
    snprintf(tarCmd, sizeof(tarCmd), "find %s -type f \\( ", path);

    char *ext = strtok(strdup(extensions), " ");
    while (ext != NULL) {
        snprintf(tarCmd + strlen(tarCmd), sizeof(tarCmd) - strlen(tarCmd), "-iname '*.%s' -o ", ext);
        ext = strtok(NULL, " ");
    }

    // Remove the last " -o " from the command
    tarCmd[strlen(tarCmd) - 3] = '\0';

    snprintf(tarCmd + strlen(tarCmd), sizeof(tarCmd) - strlen(tarCmd), " \\) -exec tar -cf %s {} +", tarFileName);

    system(tarCmd);

    // Open and send the tar file
    FILE *tarFile = fopen(tarFileName, "rb");
    if (!tarFile) {
        perror("Failed to open tar file");
        send(clientSocket, "Failed to open tar file\n", 25, 0);
        return;
    }

    fseek(tarFile, 0, SEEK_END);
    long tarFileSize = ftell(tarFile);
    rewind(tarFile);

    char *tarBuffer = (char *)malloc(tarFileSize);
    if (!tarBuffer) {
        perror("Failed to allocate memory for tar buffer");
        fclose(tarFile);
        send(clientSocket, "Memory allocation error\n", 24, 0);
        return;
    }

    fread(tarBuffer, 1, tarFileSize, tarFile);
    fclose(tarFile);
    send(clientSocket, "Tar file Created\n", strlen("Tar File Created\n"), 0);


    // Send the tar file size to the client
    send(clientSocket, &tarFileSize, sizeof(long), 0);

    // Send the tar file data to the client
    send(clientSocket, tarBuffer, tarFileSize, 0);
}

void sendFilesBeforeDate(const char *date_str, const char *path, int clientSocket) {
    char response[BUFFER_SIZE] = {0};
    char tarFileName[] = "temp.tar.gz";

    // Convert date string to time_t
    struct tm tm_date = {0};
    if (strptime(date_str, "%Y-%m-%d", &tm_date) == NULL) {
        send(clientSocket, "Invalid date format\n", 20, 0);
        return;
    }
    tm_date.tm_hour = 23;   // Set the time to the end of the day
    tm_date.tm_min = 59;
    tm_date.tm_sec = 59;

    time_t target_time = mktime(&tm_date);

    // Create a temporary tar file
    char tarCmd[512];
    snprintf(tarCmd, sizeof(tarCmd), "find %s -type f ! -newermt '%s' -exec tar -cf %s {} +", path, date_str, tarFileName);

    system(tarCmd);

    // Open and send the tar file
    FILE *tarFile = fopen(tarFileName, "rb");
    if (!tarFile) {
        perror("Failed to open tar file");
        send(clientSocket, "Failed to open tar file\n", 25, 0);
        return;
    }

    fseek(tarFile, 0, SEEK_END);
    long tarFileSize = ftell(tarFile);
    rewind(tarFile);

    char *tarBuffer = (char *)malloc(tarFileSize);
    if (!tarBuffer) {
        perror("Failed to allocate memory for tar buffer");
        fclose(tarFile);
        send(clientSocket, "Memory allocation error\n", 24, 0);
        return;
    }

    fread(tarBuffer, 1, tarFileSize, tarFile);
    fclose(tarFile);
    send(clientSocket, "Received\n", strlen("Received\n"), 0);

    // Send the tar file size to the client
    send(clientSocket, &tarFileSize, sizeof(long), 0);

    // Send the tar file data to the client
    send(clientSocket, tarBuffer, tarFileSize, 0);
}

void sendFilesAfterDate(const char *date_str, const char *path, int clientSocket) {
    char response[BUFFER_SIZE] = {0};
    char tarFileName[] = "temp.tar.gz";

    // Convert date string to time_t
    struct tm tm_date = {0};
    if (strptime(date_str, "%Y-%m-%d", &tm_date) == NULL) {
        send(clientSocket, "Invalid date format\n", 20, 0);
        return;
    }
    tm_date.tm_hour = 23;   // Set the time to the end of the day
    tm_date.tm_min = 59;
    tm_date.tm_sec = 59;

    time_t target_time = mktime(&tm_date);

    // Create a temporary tar file
    char tarCmd[512];
    snprintf(tarCmd, sizeof(tarCmd), "find %s -type f  -newermt '%s' -exec tar -cf %s {} +", path, date_str, tarFileName);

    system(tarCmd);

    // Open and send the tar file
    FILE *tarFile = fopen(tarFileName, "rb");
    if (!tarFile) {
        perror("Failed to open tar file");
        send(clientSocket, "Failed to open tar file\n", 25, 0);
        return;
    }

    fseek(tarFile, 0, SEEK_END);
    long tarFileSize = ftell(tarFile);
    rewind(tarFile);

    char *tarBuffer = (char *)malloc(tarFileSize);
    if (!tarBuffer) {
        perror("Failed to allocate memory for tar buffer");
        fclose(tarFile);
        send(clientSocket, "Memory allocation error\n", 24, 0);
        return;
    }

    fread(tarBuffer, 1, tarFileSize, tarFile);
    fclose(tarFile);
    send(clientSocket, "Received\n", strlen("Received\n"), 0);


    // Send the tar file size to the client
    send(clientSocket, &tarFileSize, sizeof(long), 0);

    // Send the tar file data to the client
    send(clientSocket, tarBuffer, tarFileSize, 0);
}

void handleClient(int clientSocket) {
    char buffer[BUFFER_SIZE];
    int bytesRead;
    char path[PATH_MAX] = "/home/anzums";  // Corrected from MAX_PATH to PATH_MAX

    while ((bytesRead = recv(clientSocket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytesRead] = '\0';
        printf("Received from client: %s\n", buffer);

        if (strncmp(buffer, "dirlist -a", 10) == 0) {
            performDirectoryListingAlphabetically(clientSocket);  // Sort alphabetically
        } else if (strncmp(buffer, "dirlist -t", 10) == 0) {
            performDirectoryListingByCreationTime(clientSocket);  // Sort by time
        } else if (strncmp(buffer, "w24fn ", 6) == 0) {
            char *filename = buffer + 6;
            sendFileInformation(filename, path, clientSocket);
        } else if (strncmp(buffer, "w24fz ", 6) == 0) {
            long size1, size2;
            if (sscanf(buffer + 6, "%ld %ld", &size1, &size2) == 2 && size1 >= 0 && size2 >= 0) {
                sendFilesInRange(size1, size2, path, clientSocket);
            } else {
                send(clientSocket, "Invalid size range\n", 19, 0);
            }
        } else if (strncmp(buffer, "w24ft ", 6) == 0) {
            char *extensions = buffer + 6;
            sendFilesByExtension(extensions, path, clientSocket);
        } else if (strncmp(buffer, "w24fdb ", 7) == 0) {
            char *date_str = buffer + 7;
            sendFilesBeforeDate(date_str, path, clientSocket);
        }
        else if (strncmp(buffer, "w24fda ", 7) == 0) {
            char *date_str = buffer + 7;
            sendFilesAfterDate(date_str, path, clientSocket);
        }
         else if (strcmp(buffer, "quitc") == 0) {
            printf("Client has requested to quit.\n");
            break;
        } else {
            char response[BUFFER_SIZE] = "Command not recognized\n";
            send(clientSocket, response, strlen(response), 0);
        }
    }

    printf("Client disconnected.\n");
    close(clientSocket);
}

int main() {
    int serverSocket, clientSocket;
    struct sockaddr_in serverAddr, clientAddr;
    socklen_t addrSize;

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        perror("Socket creation failed");
        return -1;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Bind failed");
        close(serverSocket);
        return -1;
    }

    if (listen(serverSocket, 5) < 0) {
        perror("Listen failed");
        close(serverSocket);
        return -1;
    }

    printf("Mirror1 is listening on %d...\n", PORT);

    // int connection_count = 1;

    while (1) {
        addrSize = sizeof(clientAddr);
        clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrSize);
        if (clientSocket < 0) {
            perror("Accept failed");
            continue;
        }
        printf("Connection accepted from %s:%d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));

        if (!fork()) {  // Child process
            close(serverSocket);
            handleClient(clientSocket);
            exit(0);
        }
        close(clientSocket);  // Parent closes the handled socket
    }

    return 0;
}