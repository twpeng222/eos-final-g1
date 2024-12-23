#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <signal.h>
#include <sys/wait.h>

#define BUFFER_SIZE 512
#define TRAIN_AMOUNT 10
#define POINT_AMOUNT 5

const char Points[5][20] = {"Taipei", "Taoyuan", "Taichung", "Tainan", "Kaohsiung"};
typedef struct {
    int train_id;        
    int start[TRAIN_AMOUNT];
    int destination[TRAIN_AMOUNT]; 
    int seats[TRAIN_AMOUNT];          
} TrainInfo;

TrainInfo* train;
int shm_id;
char input_buffer[BUFFER_SIZE];
int pipefd1[2], pipefd2[2]; // 管道用於父子進程通信
char buffer[BUFFER_SIZE];


void handle_signal(int sig) {
    // 子進程接收到信號後從管道讀取輸入並處理
    read((sig == SIGUSR1) ? pipefd1[0] : pipefd2[0], buffer, BUFFER_SIZE);
    // printf("Child %d received: %s\n", getpid(), buffer);
}


int handle_point(char *point) {
    for (int i = 0; i < POINT_AMOUNT; i++) {
        if (strncmp(point, Points[i], strlen(Points[i])) == 0) {
            return i;
        }
    }
    return -1;
}

void parse_train_info(char server_data[BUFFER_SIZE], TrainInfo* train) {
    char *line = strtok(server_data, "\n"); 
    int train_count = 0;
    int trainID, remaining_seats;
    char start[20],point[20];
    while (line != NULL && train_count < TRAIN_AMOUNT) {
        printf("\n%s",line);
        sscanf(line, "Direct Train %d from %s to %s (Seats: %d)", 
               &trainID, 
               start,
               point, 
               &remaining_seats);
        train->destination[trainID] = handle_point(point);
        train->start[trainID] = handle_point(start);  
        train->seats[trainID] = remaining_seats;
        // printf("\nfarest Train %d to %s (Seats: %d)",trainID,point,remaining_seats);
        // printf("\nfarest Train %d from %d to %d (Seats: %d)",trainID,train->start[trainID] ,train->destination[trainID],train->seats[trainID]);
        (train_count)++; 
        line = strtok(NULL, "\n"); 
    }
}



void connect_to_server(const char *server_ip, int server_port, int pipefd_read) {
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

    printf("Child process connected to server %s:%d\n", server_ip, server_port);

    // 註冊信號處理函數
    signal((pipefd_read == pipefd1[0]) ? SIGUSR1 : SIGUSR2, handle_signal);

    while (1) {
        // 等待信號喚醒
        pause();

        // 從管道讀取父進程傳遞的資料
        
        // read(pipefd_read, buffer, BUFFER_SIZE);
        // printf("%s\n", buffer);
        // 傳送資料到伺服器
        if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
            perror("Send failed");
            break;
        }

        // 接收伺服器回應
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            printf("\nResponse from server on port %d: %s\n", server_port, buffer);
            fflush(stdout);
        } else if (bytes_received == 0) {
            printf("\nServer on port %d closed the connection.\n", server_port);
            break;
        } else {
            perror("Receive failed");
            break;
        }
    }

    close(sockfd);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <ip> <port1> <port2>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int server_port1 = atoi(argv[2]);
    int server_port2 = atoi(argv[3]);

    // 創建管道
    if (pipe(pipefd1) == -1 || pipe(pipefd2) == -1) {
        perror("Pipe creation failed");
        exit(EXIT_FAILURE);
    }

    pid_t pid1 = fork();
    if (pid1 == 0) {
        // 子進程 1：連接到第一個伺服器端口
        close(pipefd1[1]); // 關閉寫端
        connect_to_server(server_ip, server_port1, pipefd1[0]);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        // 子進程 2：連接到第二個伺服器端口
        close(pipefd2[1]); // 關閉寫端
        connect_to_server(server_ip, server_port2, pipefd2[0]);
    }

    // 父進程
    close(pipefd1[0]); // 關閉讀端
    close(pipefd2[0]); // 關閉讀端

    while (1) {
        // 從終端讀取輸入
        printf("Enter your command (type 'exit' to quit): ");
        fgets(input_buffer, BUFFER_SIZE, stdin);

        input_buffer[strcspn(input_buffer, "\n")] = '\0'; // 移除換行符

        if (strcmp(input_buffer, "exit") == 0) {
            printf("Exiting...\n");
            kill(pid1, SIGKILL); // 終止子進程
            kill(pid2, SIGKILL);
            break;
        }

        // 將輸入寫入管道
        write(pipefd1[1], input_buffer, strlen(input_buffer) + 1);
        write(pipefd2[1], input_buffer, strlen(input_buffer) + 1);

        // 發送信號喚醒子進程
        kill(pid1, SIGUSR1);
        kill(pid2, SIGUSR2);
    }

    close(pipefd1[1]);
    close(pipefd2[1]);

    wait(NULL); // 等待子進程結束
    wait(NULL);

    printf("Parent process terminated.\n");
    return 0;
}
