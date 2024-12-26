#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 512

void interact_with_server(int sock) {
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    printf("Welcome to the Train Booking System!\n");
    printf("To quit, type 'quit'.\n");

    while (1) {
        // 提示使用者輸入訂票資訊
        printf("\nEnter booking request (format: <start_idx> <dest_idx> <time> <amount> <ID>):\n");
        fgets(buffer, BUFFER_SIZE, stdin);

        // 去除輸入的換行符
        buffer[strcspn(buffer, "\n")] = '\0';

        // 如果輸入 quit，通知伺服器並退出程式
        if (strcmp(buffer, "quit") == 0) {
            // 向伺服器發送 quit 通知
            if (write(sock, buffer, strlen(buffer)) < 0) {
                perror("Failed to send quit message to server");
            }
            printf("Exiting the system. Goodbye!\n");
            break;
        }

        // 發送請求到伺服器
        if (write(sock, buffer, strlen(buffer)) < 0) {
            perror("Failed to send message to server");
            break;
        }

        // 接收伺服器回應
        int read_size = read(sock, response, BUFFER_SIZE - 1);
        if (read_size <= 0) {
            printf("Disconnected from server.\n");
            break;
        }

        response[read_size] = '\0'; // 確保字串結尾
        printf("Server response:\n%s\n", response);
    }
}

int main() {
    int sock;
    struct sockaddr_in server;

    // 創建客戶端 socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Could not create socket");
        return 1;
    }

    printf("Socket created.\n");

    // 設定伺服器地址
    server.sin_family = AF_INET;
    server.sin_port = htons(7777); // 與伺服器設定相同的埠號
    server.sin_addr.s_addr = inet_addr("127.0.0.1"); // 本地回環地址

    // 連接伺服器
    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        perror("Connection failed");
        close(sock);
        return 1;
    }

    printf("Connected to server at 127.0.0.1.\n");

    // 與伺服器互動
    interact_with_server(sock);

    // 關閉 socket
    close(sock);
    return 0;
}