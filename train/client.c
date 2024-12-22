#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 512

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <ip> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);

    // 創建 socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid IP address");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server %s:%d\n", server_ip, server_port);

    char buffer[BUFFER_SIZE];
    while (1) {
        printf("Enter your command (type 'exit' to quit): ");
        memset(buffer, 0, BUFFER_SIZE);
        fgets(buffer, BUFFER_SIZE, stdin);

        buffer[strcspn(buffer, "\n")] = '\0';

        if (strcmp(buffer, "exit") == 0) {
            printf("Closing connection...\n");
            break;
        }

        if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
            perror("Send failed");
            break;
        }

        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received < 0) {
            perror("Receive failed");
            break;
        } else if (bytes_received == 0) {
            printf("Server disconnected.\n");
            break;
        }
        printf("Server response: %s\n", buffer);
    }

    close(sockfd);
    printf("Client terminated.\n");
    return 0;
}
