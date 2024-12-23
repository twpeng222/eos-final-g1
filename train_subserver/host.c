#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <signal.h>
#include <sys/time.h>

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
// TrainInfo train_data; 
TrainInfo* train; 
int shm_id;
int sem_id;

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

void connect_to_server(const char *server_ip, int server_port){

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
        

        // printf("Server response: %s\n", buffer);

        parse_train_info(buffer, train);

    }
    close(sockfd);
    printf("Client terminated.\n");
}



int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <ip> <port> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int server_port1 = atoi(argv[2]);
    int server_port2 = atoi(argv[3]);

    key_t key = ftok("server.c", 1);
    shm_id = shmget(key, sizeof(TrainInfo), IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }
    train = (TrainInfo *)shmat(shm_id, NULL, 0);
    for (int i = 0; i < 2;i++) {
        pid_t pid = fork();
        if (pid > 0) {
            connect_to_server(server_ip, atoi(argv[i+2]));
        }
    }
    // int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    // if (sockfd < 0) {
    //     perror("Socket creation failed");
    //     exit(EXIT_FAILURE);
    // }

    // struct sockaddr_in server_addr;
    // memset(&server_addr, 0, sizeof(server_addr));
    // server_addr.sin_family = AF_INET;
    // server_addr.sin_port = htons(server_port);

    // if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
    //     perror("Invalid IP address");
    //     close(sockfd);
    //     exit(EXIT_FAILURE);
    // }

    // if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    //     perror("Connection to server failed");
    //     close(sockfd);
    //     exit(EXIT_FAILURE);
    // }

    // printf("Connected to server %s:%d\n", server_ip, server_port);

    // char buffer[BUFFER_SIZE];
    // while (1) {
    //     printf("Enter your command (type 'exit' to quit): ");
    //     memset(buffer, 0, BUFFER_SIZE);
    //     fgets(buffer, BUFFER_SIZE, stdin);

    //     buffer[strcspn(buffer, "\n")] = '\0';

    //     if (strcmp(buffer, "exit") == 0) {
    //         printf("Closing connection...\n");
    //         break;
    //     }

    //     if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
    //         perror("Send failed");
    //         break;
    //     }

    //     memset(buffer, 0, BUFFER_SIZE);

    //     int bytes_received = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
    //     if (bytes_received < 0) {
    //         perror("Receive failed");
    //         break;
    //     } else if (bytes_received == 0) {
    //         printf("Server disconnected.\n");
    //         break;
    //     }
    //     // printf("Server response: %s\n", buffer);

    //     parse_train_info(buffer, train);

    // }
    // close(sockfd);
    // printf("Client terminated.\n");
    return 0;
}
