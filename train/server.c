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

int shm_id;
int sem_id;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
} TrainTime;

typedef struct {
    int seats[TRAIN_AMOUNT][POINT_AMOUNT - 1]; // 每段座位數
    TrainTime schedule[TRAIN_AMOUNT][POINT_AMOUNT];
    int direction[TRAIN_AMOUNT]; // 火車方向：1 = 順向, -1 = 逆向
} TrainServer;

TrainServer *shared_data;
const char line[5][20] = {"Taipei", "Taoyuan", "Taichung", "Tainan", "Kaohsiung"};
struct sembuf p_op = {0, -1, SEM_UNDO}; 
struct sembuf v_op = {0, 1, SEM_UNDO};  



// 解碼站點名稱
int handle_point(char *point) {
    for (int i = 0; i < POINT_AMOUNT; i++) {
        if (strncmp(point, line[i], strlen(line[i])) == 0) {
            return i;
        }
    }
    return -1;
}

// 解碼時間
void decode_time(TrainTime *time, char *a) {
    sscanf(a, "%d/%d/%d/%d:%d", &time->year, &time->month, &time->day, &time->hour, &time->minute);
}


int isEarlier(TrainTime *t1, TrainTime *t2) {
    if (t1->year != t2->year) return t1->year < t2->year;
    if (t1->month != t2->month) return t1->month < t2->month;
    if (t1->day != t2->day) return t1->day < t2->day;
    if (t1->hour != t2->hour) return t1->hour < t2->hour;
    return t1->minute < t2->minute;
}


void initialize_train_data() {
    for (int i = 0; i < TRAIN_AMOUNT; i++) {
        int direction = (i < TRAIN_AMOUNT / 2) ? 1 : -1; // 順向為 1，逆向為 -1
        shared_data->direction[i] = direction;

        int start_hour = (i % (TRAIN_AMOUNT / 2)) + 6; // 每台火車從不同時間起點開始

        for (int j = 0; j < POINT_AMOUNT; j++) {
            int point = (direction == 1) ? j : POINT_AMOUNT - j - 1;

            shared_data->schedule[i][point].year = 2024;
            shared_data->schedule[i][point].month = 12;
            shared_data->schedule[i][point].day = 21;
            shared_data->schedule[i][point].hour = start_hour + j;
            shared_data->schedule[i][point].minute = 0;
            if (j < POINT_AMOUNT - 1) {
                shared_data->seats[i][j] = 100; // 初始座位數
            }
        }
    }
}

// 
// int calculate_remaining_seats(int train_index, int start, int dest) {
//     int min_seats = 100;
//     if (start < dest) {
//         for (int i = start; i < dest; i++) {
//             if (shared_data->seats[train_index][i] < min_seats) {
//                 min_seats = shared_data->seats[train_index][i];
//             }
//         }
//     } else {
//         for (int i = start - 1; i >= dest; i--) {
//             if (shared_data->seats[train_index][i] < min_seats) {
//                 min_seats = shared_data->seats[train_index][i];
//             }
//         }
//     }
//     return min_seats;
// }


void search_train(int train_list[TRAIN_AMOUNT], int start, int dest, TrainTime *time) {
    for (int i = 0; i < TRAIN_AMOUNT; i++) {
        int direction = shared_data->direction[i];
        if ((direction == 1 && start < dest) || (direction == -1 && start > dest)) {
            if (isEarlier(time, &shared_data->schedule[i][start])) {
                train_list[i] = 1;
            } else {
                train_list[i] = 0;
            }
        } else {
            train_list[i] = 0;
        }
    }
}


void search_transfer(int start, int dest, TrainTime *time, char *response) {
    for (int mid = 0; mid < POINT_AMOUNT; mid++) {
        if (mid == start || mid == dest) continue;

        int first_train[TRAIN_AMOUNT] = {0};
        int second_train[TRAIN_AMOUNT] = {0};

        search_train(first_train, start, mid, time);

        for (int i = 0; i < TRAIN_AMOUNT; i++) {
            if (first_train[i]) {
                int first_seats = calculate_remaining_seats(i, start, mid);
                TrainTime *arrival_time = &shared_data->schedule[i][mid];
                search_train(second_train, mid, dest, arrival_time);

                for (int j = 0; j < TRAIN_AMOUNT; j++) {
                    if (second_train[j]) {
                        int second_seats = calculate_remaining_seats(j, mid, dest);
                        char temp[100];
                        sprintf(temp, "Take Train %d to %s (Seats: %d), then Train %d to %s (Seats: %d)\n",
                                i, line[mid], first_seats, j, line[dest], second_seats);
                        strcat(response, temp);
                    }
                }
            }
        }
    }
}
int calculate_remaining_seats(int train_index, int start, int dest) {
    int min_seats = 100; 
    if (start < dest) {
        for (int i = start; i < dest; i++) {
            if (shared_data->seats[train_index][i] < min_seats) {
                min_seats = shared_data->seats[train_index][i];
            }
        }
    } else {
        for (int i = start - 1; i >= dest; i--) {
            if (shared_data->seats[train_index][i] < min_seats) {
                min_seats = shared_data->seats[train_index][i];
            }
        }
    }
    return min_seats;
}


int update_seats(int train_index, int start, int dest, int tickets) {
    semop(sem_id, &p_op, 1);

    if (start < dest) {
        for (int i = start; i < dest; i++) {
            if (shared_data->seats[train_index][i] < tickets) {
                semop(sem_id, &v_op, 1);
                return -1;
            }
        }
        for (int i = start; i < dest; i++) {
            shared_data->seats[train_index][i] -= tickets;
        }
    } else {
        for (int i = start - 1; i >= dest; i--) {
            if (shared_data->seats[train_index][i] < tickets) {
                semop(sem_id, &v_op, 1); 
                return -1;
            }
        }
        for (int i = start - 1; i >= dest; i--) {
            shared_data->seats[train_index][i] -= tickets;
        }
    }

    semop(sem_id, &v_op, 1);
    return 0; 
}



void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE];
    int bytes_read;
    pid_t pid=fork();
    if (pid>0) {
        while ((bytes_read = recv(client_sock, buffer, BUFFER_SIZE, 0)) > 0) {
            buffer[bytes_read] = '\0';

            if (strncmp(buffer, "check_schedule", 14) == 0) {
                char start[20], dest[20], time_str[20];
                TrainTime start_time;
                int start_index, dest_index;
                int train_list[TRAIN_AMOUNT] = {0};

                sscanf(buffer + 15, "%s %s %s", start, dest, time_str);
                decode_time(&start_time, time_str);

                start_index = handle_point(start);
                dest_index = handle_point(dest);

                if (start_index == -1 || dest_index == -1) {
                    const char *error_msg = "Invalid station name\n";
                    send(client_sock, error_msg, strlen(error_msg), 0);
                    continue;
                }

                search_train(train_list, start_index, dest_index, &start_time);

                char response[BUFFER_SIZE] = "Available trains:\n";
                for (int i = 0; i < TRAIN_AMOUNT; i++) {
                    if (train_list[i]) {
                        int remaining_seats = calculate_remaining_seats(i, start_index, dest_index);
                        char temp[100];
                        sprintf(temp, "Direct Train %d to %s (Seats: %d)\n",
                                i, line[dest_index], remaining_seats);
                        strcat(response, temp);
                    }
                }

                strcat(response, "\nTransfer options:\n");
                search_transfer(start_index, dest_index, &start_time, response);

                send(client_sock, response, strlen(response), 0);
            } else if (strncmp(buffer, "book_ticket", 11) == 0) {
                char start[20], dest[20];
                int train_index, start_index, dest_index, tickets;
                char id[20];
                sscanf(buffer + 12, "%d %s %s %d %s", &train_index,start, dest, &tickets, id);
                printf("receive %d %s %s %d %s", train_index, start, dest, tickets, id);
                fflush(stdout);

                start_index = handle_point(start);
                dest_index = handle_point(dest);

                if (update_seats(train_index, start_index, dest_index, tickets) == 0) {
                    char response[BUFFER_SIZE];
                    sprintf(response, "Booking confirmed for Train %d from %s to %s. Tickets: %d. ID: %s\n",
                            train_index, line[start_index], line[dest_index], tickets, id);
                    send(client_sock, response, strlen(response), 0);
                } else {
                    char response[BUFFER_SIZE] = "Booking failed. Not enough seats.\n";
                    send(client_sock, response, strlen(response), 0);
                }
            } else {
                const char *error_msg = "Unknown command\n";
                send(client_sock, error_msg, strlen(error_msg), 0);
            }
        }

        close(client_sock);
    }
}

// 主程式
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);

    key_t key = ftok("server.c", 1);
    key_t sem_key = ftok("server.c", 2);
    shm_id = shmget(key, sizeof(TrainServer), IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }

    sem_id = semget(sem_key, 1, IPC_CREAT | 0666);
    semctl(sem_id, 0, SETVAL, 1); 

    shared_data = (TrainServer *)shmat(shm_id, NULL, 0);
    if (shared_data == (void *)-1) {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }

    initialize_train_data();

    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_sock, 5);

    printf("Server listening on port %d\n", port);

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        printf("Client connected\n");
        handle_client(client_sock);
    }

    close(server_sock);
    return 0;
}
