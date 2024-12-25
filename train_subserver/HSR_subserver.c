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
#include <sys/wait.h>

#define BUFFER_SIZE 512
#define TRAIN_AMOUNT 10 // 10 台火車
#define POINT_AMOUNT 5
#define SEAT_AMOUNT 10
#define MAX_BOOKINGS 1000


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
    char id[20];         // 訂票人的 ID
    int train_index;     // 火車編號
    int start_index;     // 起點站索引
    int dest_index;      // 終點站索引
    int tickets;         // 訂票數量
    int seat_numbers[10]; // 座位號 (假設最多一次訂10張票)
} BookingRecord;

typedef struct {
    int seats[TRAIN_AMOUNT][POINT_AMOUNT - 1]; // 區間座位數
    int total_seats[TRAIN_AMOUNT];            // 每列火車的總可用座位數
    int seat_allocation[TRAIN_AMOUNT][SEAT_AMOUNT][POINT_AMOUNT - 1]; 
    TrainTime schedule[TRAIN_AMOUNT][POINT_AMOUNT];
    int direction[TRAIN_AMOUNT];
    BookingRecord bookings[MAX_BOOKINGS];
    int booking_count;
} TrainServer;

TrainServer *shared_data;

const char line[5][20] = {"Taipei", "Taoyuan", "Taichung", "Tainan", "Kaohsiung"};
struct sembuf p_op = {0, -1, SEM_UNDO}; 
struct sembuf v_op = {0, 1, SEM_UNDO}; 




int handle_point(char *point) {
    for (int i = 0; i < POINT_AMOUNT; i++) {
        if (strncmp(point, line[i], strlen(line[i])) == 0) {
            return i;
        }
    }
    return -1;
}


void decode_time(TrainTime *time, char *a) {
    sscanf(a, "%d/%d/%d/%d:%d", &time->year, &time->month, &time->day, &time->hour, &time->minute);
}

void encode_time(TrainTime *time, char *a) {
    sprintf(a, "%04d/%02d/%02d/%02d:%02d", time->year, time->month, time->day, time->hour, time->minute);

}



int isEarlier(TrainTime *t1, TrainTime *t2) {
    if (t1->year != t2->year) return 0;
    if (t1->month != t2->month) return 0;
    if (t1->day != t2->day) return 0;
    if (t1->hour != t2->hour) return t1->hour < t2->hour;
    return t1->minute <= t2->minute;
}


void initialize_train_data() {
    for (int i = 0; i < TRAIN_AMOUNT; i++) {
        int direction = (i < TRAIN_AMOUNT / 2) ? 1 : -1;
        shared_data->direction[i] = direction;

        int start_hour = (i % (TRAIN_AMOUNT / 2)) + 6; 

        for (int j = 0; j < POINT_AMOUNT; j++) {
            int point = (direction == 1) ? j : POINT_AMOUNT - j - 1;

            shared_data->schedule[i][point].year = 2024;
            shared_data->schedule[i][point].month = 12;
            shared_data->schedule[i][point].day = 21;
            shared_data->schedule[i][point].hour = start_hour + j*3;
            shared_data->schedule[i][point].minute = 0;

            shared_data->seats[i][j] = SEAT_AMOUNT; 
            // printf("Train%d %s %d\n", i, line[point], shared_data->schedule[i][point].hour);
        }
    }
    for (int i = 0; i < TRAIN_AMOUNT; i++) {
        shared_data->total_seats[i] = SEAT_AMOUNT; // 初始化每列火車的總座位數
    }
}


int calculate_remaining_seats(int train_index, int start, int dest) {
    int min_seats = SEAT_AMOUNT; 
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

int calculate_farest_dest(int train_index, int start, int dest,int amount){
    int direction = shared_data->direction[train_index];
    int remaining_seats = 0;
    while (remaining_seats == 0){
        remaining_seats = calculate_remaining_seats(train_index, start, dest);
        if (direction == 1 && remaining_seats < amount) {
            dest--;
        } else if (direction == -1 && remaining_seats < amount){
            dest++;
        }
    }
    return dest;
}

int calculate_inverse_farest_dest(int train_index, int start, int dest){
    int direction = shared_data->direction[train_index];
    int remaining_seats = 0;
    while (remaining_seats == 0){
        remaining_seats = calculate_remaining_seats(train_index, start, dest);
        if (direction == 1 && remaining_seats==0) {
            start++;
        } else if (direction == -1 && remaining_seats==0){
            start++;
        }
    }
    return start;
}



void search_train(int train_list[TRAIN_AMOUNT], int start, int dest, TrainTime *time) {
    for (int i = 0; i < TRAIN_AMOUNT; i++) {
        int direction = shared_data->direction[i];
        if ((direction == 1 && start < dest) || (direction == -1 && start > dest)) {
            if (isEarlier( time, &shared_data->schedule[i][start])) {
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

void query_bookings_by_id(const char *id) {
    semop(sem_id, &p_op, 1);

    printf("Bookings for ID: %s\n", id);
    for (int i = 0; i < shared_data->booking_count; i++) {
        BookingRecord *record = &shared_data->bookings[i];
        if (strcmp(record->id, id) == 0) {
            printf("Change\n");
            printf("Train %d from %s to %s. Tickets: %d. Seats: ",
                   record->train_index,
                   line[record->start_index],
                   line[record->dest_index],
                   record->tickets);
            for (int j = 0; j < record->tickets; j++) {
                printf("%d ", record->seat_numbers[j]);
            }
            printf("\n");
        }
    }

    semop(sem_id, &v_op, 1);
}

int update_seats(int train_index, int start, int dest, int tickets, const char *id, int contiguous) {
    semop(sem_id, &p_op, 1); // 取得鎖

    int seat_numbers[10];
    int seat_count = 0;

    // 0) 如果起終點相同 => 直接失敗
    if (start == dest) {
        semop(sem_id, &v_op, 1);
        return -1;
    }

    printf("[DEBUG] update_seats: train=%d, start=%d, dest=%d, tickets=%d, id=%s\n",
           train_index, start, dest, tickets, id);

    // ----------------------------- 
    // (A) 分兩段: 順向 or 逆向
    // -----------------------------
    if (start < dest) {
        // 先檢查每段 seats[i] 是否 >= tickets
        for (int i = start; i < dest; i++) {
            if (shared_data->seats[train_index][i] < tickets) {
                printf("[DEBUG] seats[%d][%d] not enough => fail\n", train_index, i);
                semop(sem_id, &v_op, 1);
                return -1;
            }
        }

        // 開始分配座位 (contiguous / non-contiguous)
        if (contiguous) {
            // 找連續座位 seat=0..(SEAT_AMOUNT-tickets)
            for (int seat = 0; seat <= SEAT_AMOUNT - tickets; seat++) {
                int available = 1;
                // 檢查在 i=[start..dest-1]，seat..seat+(tickets-1) 是否都為0
                for (int i = start; i < dest && available; i++) {
                    for (int j = 0; j < tickets; j++) {
                        if (shared_data->seat_allocation[train_index][seat + j][i] != 0) {
                            available = 0;
                            break;
                        }
                    }
                }
                if (available) {
                    // 分配
                    for (int i = start; i < dest; i++) {
                        for (int j = 0; j < tickets; j++) {
                            shared_data->seat_allocation[train_index][seat + j][i] = 1;
                        }
                        shared_data->seats[train_index][i] -= tickets;
                    }
                    for (int j = 0; j < tickets; j++) {
                        seat_numbers[seat_count++] = seat + j;
                    }
                    break; // 分配成功 => 跳出
                }
            }
        } else {
            // non-contiguous
            for (int seat = 0; seat < SEAT_AMOUNT && seat_count < tickets; seat++) {
                int available = 1;
                // 檢查 i=[start..dest-1]
                for (int i = start; i < dest; i++) {
                    if (shared_data->seat_allocation[train_index][seat][i] != 0) {
                        available = 0;
                        break;
                    }
                }
                if (available) {
                    // 分配
                    for (int i = start; i < dest; i++) {
                        shared_data->seat_allocation[train_index][seat][i] = 1;
                    }
                    shared_data->seats[train_index][start] -= tickets;
                    seat_numbers[seat_count++] = seat;
                }
            }
        }
    } 
    else {
        // 逆向: start>dest
        // 檢查 seats[i = start-1..dest] 是否足夠 (站 4->3->2->1->0)
        for (int i = start - 1; i >= dest; i--) {
            if (shared_data->seats[train_index][i] < tickets) {
                printf("[DEBUG] seats[%d][%d] not enough => fail\n", train_index, i);
                semop(sem_id, &v_op, 1);
                return -1;
            }
        }

        // 分配座位
        if (contiguous) {
            for (int seat = 0; seat <= SEAT_AMOUNT - tickets; seat++) {
                int available = 1;
                // 這邊 i= start-1..dest
                for (int i = start - 1; i >= dest && available; i--) {
                    for (int j = 0; j < tickets; j++) {
                        if (shared_data->seat_allocation[train_index][seat + j][i] != 0) {
                            available = 0;
                            break;
                        }
                    }
                }
                if (available) {
                    // 分配
                    for (int i = start - 1; i >= dest; i--) {
                        for (int j = 0; j < tickets; j++) {
                            shared_data->seat_allocation[train_index][seat + j][i] = 1;
                        }
                        shared_data->seats[train_index][i] -= tickets;
                    }
                    for (int j = 0; j < tickets; j++) {
                        seat_numbers[seat_count++] = seat + j;
                    }
                    break;
                }
            }
        } else {
            for (int seat = 0; seat < SEAT_AMOUNT && seat_count < tickets; seat++) {
                int available = 1;
                // 檢查 i=[start-1..dest]
                for (int i = start - 1; i >= dest; i--) {
                    if (shared_data->seat_allocation[train_index][seat][i] != 0) {
                        available = 0;
                        break;
                    }
                }
                if (available) {
                    // 分配
                    for (int i = start - 1; i >= dest; i--) {
                        shared_data->seat_allocation[train_index][seat][i] = 1;
                    }
                    shared_data->seats[train_index][start - 1] -= tickets;
                    seat_numbers[seat_count++] = seat;
                }
            }
        }
    }

    // 若 seat_count < tickets => 回滾
    if (seat_count < tickets) {
        printf("[DEBUG] seat_count=%d < tickets=%d => rollback\n", seat_count, tickets);
        // 回滾: 把已經分出去的 seat 全部釋放掉
        if (start < dest) {
            for (int i = start; i < dest; i++) {
                for (int k = 0; k < seat_count; k++) {
                    int s = seat_numbers[k];
                    shared_data->seat_allocation[train_index][s][i] = 0;
                }
                shared_data->seats[train_index][i] += tickets;
            }
        } else {
            for (int i = start - 1; i >= dest; i--) {
                for (int k = 0; k < seat_count; k++) {
                    int s = seat_numbers[k];
                    shared_data->seat_allocation[train_index][s][i] = 0;
                }
                shared_data->seats[train_index][i] += tickets;
            }
        }
        semop(sem_id, &v_op, 1);
        return -1;
    }

    // 記錄訂票資訊
    if (shared_data->booking_count < MAX_BOOKINGS) {
        BookingRecord *record = &shared_data->bookings[shared_data->booking_count];
        strcpy(record->id, id);
        record->train_index = train_index;
        record->start_index = start;
        record->dest_index = dest;
        record->tickets = tickets;
        for (int i = 0; i < tickets; i++) {
            record->seat_numbers[i] = seat_numbers[i];
        }
        shared_data->booking_count++;
    }

    semop(sem_id, &v_op, 1); // 釋放鎖
    return 0;
}

int cancel_order(const char *id, int train_index, int cancel_num) {
    // 1) 取得鎖
    semop(sem_id, &p_op, 1);

    // 2) 在所有已存在的訂單中尋找符合者 (ID & 列車班次)
    int found_index = -1;
    for (int i = 0; i < shared_data->booking_count; i++) {
        BookingRecord *rec = &shared_data->bookings[i];
        if ((strcmp(rec->id, id) == 0) && (rec->train_index == train_index)) {
            found_index = i;
            break;
        }
    }

    // 若找不到 => 直接回傳失敗
    if (found_index == -1) {
        semop(sem_id, &v_op, 1);
        return -1; 
    }

    BookingRecord *record = &shared_data->bookings[found_index];

    // 3) 檢查要取消的人數是否超過原訂票數
    if (cancel_num > record->tickets) {
        semop(sem_id, &v_op, 1);
        return -1; // 取消的數量不合法
    }

    // 4) 開始釋放座位 (從後面開始退 cancel_num 張)
    //    假設 BookingRecord 裡的 seat_numbers 依照你之前訂時的順序存放
    //    也假設 partial cancel 只是簡單處理 => 從尾端退
    int start_seg = record->start_index;
    int dest_seg  = record->dest_index;

    // 從後往前取 seat_numbers
    int start_idx = record->tickets - cancel_num; 
    // e.g. 如果 originally 8 張，cancel_num=6，就從 seat_numbers[2..7] 退掉

    for (int i = start_idx; i < record->tickets; i++) {
        int seat_id = record->seat_numbers[i]; 
        // 釋放所有區間 (不管順向/逆向，跟你 update_seats() 時一致)
        if (start_seg < dest_seg) {
            // 順向
            for (int seg = start_seg; seg < dest_seg; seg++) {
                shared_data->seat_allocation[train_index][seat_id][seg] = 0;
                shared_data->seats[train_index][seg] += 1;
            }
        } else {
            // 逆向
            for (int seg = start_seg - 1; seg >= dest_seg; seg--) {
                shared_data->seat_allocation[train_index][seat_id][seg] = 0;
                shared_data->seats[train_index][seg] += 1;
            }
        }
    }

    // 5) 若是全部取消 => 從 bookings 裡移除這筆
    if (cancel_num == record->tickets) {
        // 把最後一筆訂單移到被移除的那筆上，並 booking_count--
        // 避免中間空洞
        shared_data->booking_count--;
        if (found_index != shared_data->booking_count) {
            // 將最後一筆搬到 found_index 位置
            shared_data->bookings[found_index] = shared_data->bookings[shared_data->booking_count];
        }
    } else {
        // 部分取消 => 更新 tickets 與 seat_numbers
        record->tickets -= cancel_num;
        // seat_numbers 從尾巴開始刪掉 cancel_num 張
        // 簡單做法：不用真的 free，直接把 record->tickets 以後的 seat_numbers 覆蓋掉 
        //  (有需要可以設 -1 或 0 表示無效)
        for (int i = 0; i < cancel_num; i++) {
            int idx = record->tickets + i; 
            record->seat_numbers[idx] = -1; // 或者設0，表示無效
        }
    }

    // 6) 釋放鎖，回傳成功
    semop(sem_id, &v_op, 1);
    return 0;
}

// 處理客戶端請求
void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE];
    int bytes_read;
    pid_t pid = fork();
    if (pid > 0) {
        while ((bytes_read = recv(client_sock, buffer, BUFFER_SIZE, 0)) > 0) {
            buffer[bytes_read] = '\0';
            printf("check_schedule");
            fflush(stdout);

            if (strncmp(buffer, "check_schedule", 14) == 0) {
                // 處理 check_schedule 指令的邏輯
                char start[20], dest[20], time_str[20];
                TrainTime start_time;
                int start_index, dest_index, amount;
                int train_list[TRAIN_AMOUNT] = {0};

                sscanf(buffer + 15, "%s %s %s %d", start, dest, time_str, &amount);
                decode_time(&start_time, time_str);

                start_index = handle_point(start);
                dest_index = handle_point(dest);

                if (start_index == -1 || dest_index == -1) {
                    const char *error_msg = "Invalid station name\n";
                    send(client_sock, error_msg, strlen(error_msg), 0);
                    continue;
                }

                search_train(train_list, start_index, dest_index, &start_time);

                char response[BUFFER_SIZE] = "";
                for (int i = 0; i < TRAIN_AMOUNT; i++) {
                    if (train_list[i]) {
                        int farest_index = calculate_farest_dest(i, start_index, dest_index, amount);
                        int remaining_seats = calculate_remaining_seats(i, start_index, farest_index);
                        char temp[512], time[20];
                        encode_time(shared_data->schedule[i], time);
                        sprintf(temp, "Farest Train %d from %s to %s (Seats: %d) at %s\n",
                                i, line[start_index], line[farest_index], remaining_seats, time);
                        strcat(response, temp);
                        break;
                    }
                }
                send(client_sock, response, strlen(response), 0);
            } else if (strncmp(buffer, "check_order", 11) == 0) {
                char id[20];
                sscanf(buffer + 12, "%s", id);
                printf("Querying bookings for ID: %s\n", id);
                fflush(stdout);

                semop(sem_id, &p_op, 1);

                char response[BUFFER_SIZE] = "";
                int found = 0;

                // 遍歷所有訂單，按起點、終點和列車班次進行分組
                for (int i = 0; i < shared_data->booking_count; i++) {
                    BookingRecord *record = &shared_data->bookings[i];
                    if (strcmp(record->id, id) == 0) {
                        found = 1;

                        // 格式化每筆訂單
                        char booking_info[1000];
                        char seat_list[BUFFER_SIZE] = "";
                        for (int j = 0; j < record->tickets; j++) {
                            char seat_info[10];
                            sprintf(seat_info, "%d", record->seat_numbers[j]);
                            if (j > 0) strcat(seat_list, ", "); // 添加逗號分隔座位號
                            strcat(seat_list, seat_info);
                        }

                        // 合併並格式化輸出
                        sprintf(booking_info, 
                                "Train %d from %s to %s. Tickets: %d. Seats: %s\n", 
                                record->train_index, 
                                line[record->start_index], 
                                line[record->dest_index], 
                                record->tickets, 
                                seat_list);
                        strcat(response, booking_info);
                    }
                }

                semop(sem_id, &v_op, 1); // 釋放資源鎖

                if (!found) {
                    strcpy(response, "No bookings found for this ID.\n");
                }

                send(client_sock, response, strlen(response), 0); // 回傳結果給客戶端
            } else if (strncmp(buffer, "book_ticket", 11) == 0) {
                char start[20], dest[20];
                int train_index, start_index, dest_index, tickets, contiguous;
                char id[20];
                sscanf(buffer + 12, "%d %s %s %d %s %d", &train_index, start, dest, &tickets, id, &contiguous);
                printf("receive %d %s %s %d %s %d\n", train_index, start, dest, tickets, id, contiguous);
                fflush(stdout);

                start_index = handle_point(start);
                dest_index = handle_point(dest);

                if (update_seats(train_index, start_index, dest_index, tickets, id, contiguous) == 0) {
                    char response[BUFFER_SIZE];
                    sprintf(response, "Booking confirmed for Train %d from %s to %s. Tickets: %d. ID: %s\n",
                            train_index, line[start_index], line[dest_index], tickets, id);
                    send(client_sock, response, strlen(response), 0);
                } else {
                    char response[BUFFER_SIZE] = "Booking failed. Not enough seats.\n";
                    send(client_sock, response, strlen(response), 0);
                }
            } else if (strncmp(buffer, "cancel_order", 12) == 0) {
                char id[20];
                int train_index, cancel_num;
                // 格式: cancel_order <ID> <train_index> <cancel_num>
                sscanf(buffer + 13, "%s %d %d", id, &train_index, &cancel_num);

                printf("[DEBUG] cancel_order: ID=%s, train_index=%d, cancel_num=%d\n", 
                        id, train_index, cancel_num);

                // 呼叫我們剛剛寫好的函式
                if (cancel_order(id, train_index, cancel_num) == 0) {
                    // 成功
                    char msg[128];
                    sprintf(msg, "Cancel order success. ID=%s, Train=%d, Cancel=%d\n",
                            id, train_index, cancel_num);
                    send(client_sock, msg, strlen(msg), 0);
                } else {
                    // 失敗
                    char msg[128];
                    sprintf(msg, "Cancel order fail. ID=%s, Train=%d, Cancel=%d\n",
                            id, train_index, cancel_num);
                    send(client_sock, msg, strlen(msg), 0);
                }
            } else {
                const char *error_msg = "Unknown command\n";
                send(client_sock, error_msg, strlen(error_msg), 0);
            }
        }

        close(client_sock);
    }
}

void cleanup() {
    // 刪除共享記憶體
    if (shmdt(shared_data) == -1) {
        perror("shmdt failed");
    }

    if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
        perror("shmctl failed");
    }

    // 刪除信號量
    if (semctl(sem_id, 0, IPC_RMID) == -1) {
        perror("semctl failed");
    }

    printf("\nShared memory and semaphore removed. Exiting.\n");
    exit(EXIT_SUCCESS);
}

void signal_handler(int signal) {
    if (signal == SIGINT) {
        cleanup();
    }
}

void handler(int signum) {
    (void)signum;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    signal(SIGINT, signal_handler);
    signal(SIGCHLD, handler);

    int port = atoi(argv[1]);

    key_t key = ftok("train_subserver.c", 1);
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
