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
// #include 

#define BUFFER_SIZE 512
#define TRAIN_AMOUNT 10
#define POINT_AMOUNT 5

const char Points[5][20] = {"Taipei", "Taoyuan", "Taichung", "Tainan", "Kaohsiung"}; // 3 ~ 7 高鐵
// const char Points2[5][20] = {"Taoyuan", "Taipei", "Yilan","Hualien","Taitung"};
const char Points2[5][20] = {"Taitung","Hualien","Yilan","Taipei","Taoyuan"}; // 0 ~ 4 火車

int shm_id;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
} TrainTime;

typedef struct {
    int train_id[2];        
    int start[2];
    int destination[2]; 
    int seats[2];   
    TrainTime reach[2];
} TrainInfo;

TrainInfo *train;

int shm_id;
char input_buffer[BUFFER_SIZE];
int pipefd1[2], pipefd2[2]; // 管道用於父子進程通信
char buffer[BUFFER_SIZE];
pid_t pid1, pid2;

void handle_signal(int sig) {
    // 子進程接收到信號後從管道讀取輸入並處理
    read((sig == SIGUSR1) ? pipefd1[0] : pipefd2[0], buffer, BUFFER_SIZE);
    // printf("Child %d received: %s\n", getpid(), buffer);
}
void decode_time(TrainTime *time, char *a) {
    sscanf(a, "%d/%d/%d/%d:%d", &time->year, &time->month, &time->day, &time->hour, &time->minute);
}
void encode_time(TrainTime *time, char *a) {
    sprintf(a, "%04d/%02d/%02d/%02d:%02d", time->year, time->month, time->day, time->hour, time->minute);

}

int handle_point(char *point) {
    for (int i = 0; i < POINT_AMOUNT; i++) {
        if (strncmp(point, Points[i], strlen(Points[i])) == 0) {
            return i+3;
        }
        if (strncmp(point, Points2[i], strlen(Points[i])) == 0) {
            return i;
        }
    }
    return -1;
}

void parse_train_info(char server_data[BUFFER_SIZE],TrainInfo *train, int id) {
    char *line = strtok(server_data, "\n"); 
    int train_count = 0;
    int trainID, remaining_seats;
    char start[20],point[20],time[20];
    while (line != NULL && train_count < TRAIN_AMOUNT) {
        // printf("\n%s",line);
        sscanf(line, "Farest Train %d from %s to %s (Seats: %d) at %s", 
               &trainID, 
               start,
               point, 
               &remaining_seats,
               time);
        train->destination[id] = handle_point(point);
        train->start[id] = handle_point(start);  
        train->seats[id] = remaining_seats;
        train->train_id[id] = trainID;
        decode_time(&train->reach[id], time);
        // printf("\nfarest Train %d to %s (Seats: %d)",trainID,point,remaining_seats);
        // printf("\nfarest Train %d from %d to %d (Seats: %d)",trainID,train->start[trainID] ,train->destination[trainID],train->seats[trainID]);
        (train_count)++; 
        line = strtok(NULL, "\n"); 
    }
}



void connect_to_server(const char *server_ip, int server_port, int pipefd_read, int serverID) {
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

    signal((pipefd_read == pipefd1[0]) ? SIGUSR1 : SIGUSR2, handle_signal);

    while (1) {
        pause();

        if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
            perror("Send failed");
            break;
        }

        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            // printf("\nResponse from server on port %d: %s\n", server_port, buffer);
            parse_train_info(buffer, train,serverID);
            // fflush(stdout);
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
// 3 case: 1.HSR 2.train 3.both 
// int tell_case(int start, int end){
//     if (start < end){
//         if ( start > 2 ){
//             return 1;
//         } else if ( ){
//             printf("train\n");
//         }
//     }
// }

// 1 :train 0 :HSR

void check_schedule(int start, int end, char time[20], int serverID, int amount){
    char response[BUFFER_SIZE];

    if ( serverID){
        sprintf(response,"check_schedule %s %s %s %d",Points2[start], Points2[end], time,amount);
        write(pipefd2[1], response, strlen(response) + 1);
        kill(pid2, SIGUSR2);
    } else{
        sprintf(response,"check_schedule %s %s %s %d",Points[start-3], Points[end-3], time,amount);
        write(pipefd1[1], response, strlen(response) + 1);
        kill(pid1, SIGUSR1);
    }
}

void schedule_transfer(int start, int end,char time[20], int amount){
    if (start < end){
        if ( start < 3 && end > 4){
            // printf("schedule check train %d ~ 4\n",start);
            check_schedule(start,4,time,1,amount); //result in id 1
            sleep(1);
            // printf("schedule check HSR 3 ~ %d \n",end);
            char time2[20] = {0};
            encode_time(&train->reach[1],time2);
            check_schedule(train->destination[1],end,time2,0,amount);
            sleep(1);
            if (train->destination[0] == end){
                printf("\nTake train(%d) from %s to %s (seats %d), then take high speed rail(%d) from %s to %s (seats %d)\n",train->train_id[1], Points2[start], Points2[train->destination[1]],train->seats[1],train->train_id[0],Points[train->start[0]-3],Points[train->destination[0]-3],train->seats[0] );
            } else {
                printf("\nThere's no High speed rail available\n");
            }
        } else if ( start == 3 ){
            // printf("schedule check HSR %d ~ %d\n",start,end);
            check_schedule(start,end,time,0,amount);
            sleep(1);
            if (train->destination[0] == 3){
                check_schedule(3,4,time,1,amount);
                sleep(1);
                check_schedule(4,end,time,0,amount);
                sleep(1);
                if (train->destination[0] == end){
                    printf("\nTake train(%d) from %s to %s (seats %d), then take high speed rail(%d) from %s to %s (seats %d)\n",train->train_id[1], Points2[start], Points2[train->destination[1]],train->seats[1],train->train_id[0],Points[train->start[0]-3],Points[train->destination[0]-3],train->seats[0] );
                } else {
                    printf("\nThere's no way available\n");
                }
            } else {
                if (train->destination[0] == end){
                    printf("\nTake high speed rail(%d) from %s to %s (seats %d)\n",train->train_id[0],Points[train->start[0]-3],Points[train->destination[0]-3],train->seats[0] );
                } else {
                    printf("\nThere's no High speed rail available\n");
                }
            }
        } else{
            // printf("schedule check train %d ~ %d\n",start,end);
            check_schedule(start,end,time,1,amount);
            sleep(1);
            if (train->destination[1] == end){
                printf("\nTake train(%d) from %s to %s (seats %d)\n",train->train_id[1], Points2[start], Points2[train->destination[1]],train->seats[1] );
            } else {
                printf("\nThere's no train available\n");
            }
        }
    } else{
        if ( end < 3 && start > 4){
            // printf("schedule check HSR %d ~ 3 \n",start);
            check_schedule(start,3,time,0,amount);
            sleep(1);
            // printf("schedule check train 2 ~ %d\n",end);
            char time2[20] = {0};
            encode_time(&train->reach[0],time2);
            check_schedule(train->destination[0],end,time2,1,amount);
            if (train->destination[1] == end){
                printf("\nTake high speed rail(%d) from %s to %s (seats %d), then take train(%d) from %s to %s (seats %d)\n",train->train_id[0], Points[start-3], Points[train->destination[0]-3],train->seats[0],train->train_id[1],Points2[train->start[1]],Points2[train->destination[1]],train->seats[1] );
            } else {
                printf("\nThere's no High speed rail available\n");
            }
        } else if ( end == 3 ){
            // printf("schedule check HSR %d ~ %d\n",start,end);
            check_schedule(start,end,time,0,amount);
            sleep(1);
            if (train->destination[0] == 4){
                check_schedule(4,3,time,1,amount);
                sleep(1);
                if (train->destination[1] == end){
                    printf("\nTake high speed rail(%d) from %s to %s (seats %d), then take train(%d) from %s to %s (seats %d)\n",train->train_id[0], Points[start-3], Points[train->destination[0]-3],train->seats[0],train->train_id[1],Points2[train->start[1]],Points2[train->destination[1]],train->seats[1] );
                } else {
                    printf("\nThere's no High speed rail available\n");
                }
            } else {
                if (train->destination[0] == end){
                    printf("\nTake high speed rail(%d) from %s to %s (seats %d)\n",train->train_id[0],Points[train->start[0]-3],Points[train->destination[0]-3],train->seats[0] );
                } else {
                    printf("\nThere's no High speed rail available\n");
                }
            }
        } else{
            // printf("schedule check train %d ~ %d\n",start,end);
            check_schedule(start,end,time,1,amount);
            sleep(1);
            if (train->destination[1] == end){
                printf("\nTake train(%d) from %s to %s (seats %d)\n",train->train_id[1], Points2[start], Points2[train->destination[1]],train->seats[1] );
            } else {
                printf("\nThere's no train available\n");
            }
        }     
    }
}





void handle_input(char *buffer){
    if (strncmp(buffer, "check_schedule", 14) == 0) {
        char start[20], dest[20], time_str[20];
        TrainTime start_time;
        int start_index, dest_index, amount;

        sscanf(buffer + 15, "%s %s %s %d", start, dest, time_str, &amount);
        decode_time(&start_time, time_str);

        start_index = handle_point(start);
        dest_index = handle_point(dest);

        if (start_index == -1 || dest_index == -1) {
            printf("Error\n");
        } 
        // else{
        //     printf("Client aquire %d to %d\n",start_index,dest_index);
        // }      
        schedule_transfer(start_index,dest_index,time_str,amount);
    }
}



int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <ip> <port1> <port2>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int server_port1 = atoi(argv[2]);
    int server_port2 = atoi(argv[3]);
    key_t key = ftok("host.c", 1);
    shm_id = shmget(key, sizeof(TrainInfo), IPC_CREAT | 0666);

    if (shm_id == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }
    train = (TrainInfo *)shmat(shm_id, NULL, 0);
    if (train == (void *)-1) {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }

    // 創建管道
    if (pipe(pipefd1) == -1 || pipe(pipefd2) == -1) {
        perror("Pipe creation failed");
        exit(EXIT_FAILURE);
    }

    pid1 = fork();
    if (pid1 == 0) {
        // 子進程 1：連接到第一個伺服器端口
        close(pipefd1[1]); // 關閉寫端
        connect_to_server(server_ip, server_port1, pipefd1[0],0);
    }

    pid2 = fork();
    if (pid2 == 0) {
        // 子進程 2：連接到第二個伺服器端口
        close(pipefd2[1]); // 關閉寫端
        connect_to_server(server_ip, server_port2, pipefd2[0],1);
    }

    // 父進程
    close(pipefd1[0]); // 關閉讀端
    close(pipefd2[0]); // 關閉讀端

    while (1) {
        // 從終端讀取輸入
        printf("Enter your command (type 'exit' to quit): ");
        fgets(input_buffer, BUFFER_SIZE, stdin);

        input_buffer[strcspn(input_buffer, "\n")] = '\0'; // 移除換行符
        handle_input(input_buffer); 



        if (strcmp(input_buffer, "exit") == 0) {
            printf("Exiting...\n");
            kill(pid1, SIGKILL); // 終止子進程
            kill(pid2, SIGKILL);
            break;
        }


        // write(pipefd1[1], input_buffer, strlen(input_buffer) + 1);
        // write(pipefd2[1], input_buffer, strlen(input_buffer) + 1);

        // kill(pid1, SIGUSR1);
        // kill(pid2, SIGUSR2);
    }

    close(pipefd1[1]);
    close(pipefd2[1]);

    wait(NULL); // 等待子進程結束
    wait(NULL);

    printf("Parent process terminated.\n");
    return 0;
}
