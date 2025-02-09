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
    int used;                 // 0: 未使用, 1: 已使用
    int train_index;
    int start_index;
    int dest_index;
    int total_tickets;
    int seat_numbers[100];    // 假設最多合併 100 張
    int seat_count;
} MergedBooking;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
} TrainTime;

typedef struct {
    char id[20];         
    int train_index;    
    int start_index;     
    int dest_index;     
    int tickets;         
    int seat_numbers[10]; 
} BookingRecord;

typedef struct {
    int seats[TRAIN_AMOUNT][POINT_AMOUNT - 1]; 
    int total_seats[TRAIN_AMOUNT];            
    int seat_allocation[TRAIN_AMOUNT][SEAT_AMOUNT][POINT_AMOUNT - 1]; 
    TrainTime schedule[TRAIN_AMOUNT][POINT_AMOUNT];
    int direction[TRAIN_AMOUNT];
    BookingRecord bookings[MAX_BOOKINGS];
    int booking_count;
} TrainServer;

TrainServer *shared_data;

const char line[5][20] = {"Taitung","Hualien","Yilan","Taipei","Taoyuan"};
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
    for (int i = 0; i < TRAIN_AMOUNT/2; i++) {
        int direction = (i < TRAIN_AMOUNT / 2) ? 1 : -1;
        shared_data->direction[i] = direction;

        int start_hour = ((i % (TRAIN_AMOUNT / 2)) + 21)%24; 

        for (int j = 0; j < POINT_AMOUNT; j++) {
            int point = (direction == 1) ? j : POINT_AMOUNT - j - 1;

            shared_data->schedule[i][point].year = 2024;
            shared_data->schedule[i][point].month = 12;
            if (start_hour + j*3 > 23){
                shared_data->schedule[i][point].day = 21;
                shared_data->schedule[i][point].hour = (start_hour + j*3)%24;
            } else{
                shared_data->schedule[i][point].day = 20;
                shared_data->schedule[i][point].hour = (start_hour + j*3)%24;
            }
            shared_data->schedule[i][point].minute = 0;

            shared_data->seats[i][j] = SEAT_AMOUNT; 
            // printf("Train%d %s %d\n", i, line[point], shared_data->schedule[i][point].hour);
        }
    }
    for (int i = TRAIN_AMOUNT/2; i < TRAIN_AMOUNT; i++) {
        int direction = (i < TRAIN_AMOUNT / 2) ? 1 : -1;
        shared_data->direction[i] = direction;

        int start_hour = ((i % (TRAIN_AMOUNT / 2)) + 15)%24; 

        for (int j = 0; j < POINT_AMOUNT; j++) {
            int point = (direction == 1) ? j : POINT_AMOUNT - j - 1;

            shared_data->schedule[i][point].year = 2024;
            shared_data->schedule[i][point].month = 12;
            if (start_hour + j*3 > 23){
                shared_data->schedule[i][point].day = 22;
                shared_data->schedule[i][point].hour = (start_hour + j*3)%24;
            } else{
                shared_data->schedule[i][point].day = 21;
                shared_data->schedule[i][point].hour = (start_hour + j*3)%24;
            }
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
    semop(sem_id, &p_op, 1);

    int seat_numbers[10];
    int seat_count = 0;
    if (start == dest) {
        semop(sem_id, &v_op, 1);
        return -1;
    }

    printf("[DEBUG] update_seats: train=%d, start=%d, dest=%d, tickets=%d, id=%s\n",
           train_index, start, dest, tickets, id);

    if (start < dest) {
        for (int i = start; i < dest; i++) {
            if (shared_data->seats[train_index][i] < tickets) {
                printf("[DEBUG] seats[%d][%d] not enough => fail\n", train_index, i);
                semop(sem_id, &v_op, 1);
                return -1;
            }
        }
        if (contiguous) {
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
                for (int i = start; i < dest; i++) {
                    if (shared_data->seat_allocation[train_index][seat][i] != 0) {
                        available = 0;
                        break;
                    }
                }
                if (available) {
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

void free_seats_for_record(BookingRecord *record) {
    int train_idx = record->train_index;
    int start_seg = record->start_index;
    int dest_seg  = record->dest_index;

    for (int s = 0; s < record->tickets; s++) {
        int seat_id = record->seat_numbers[s];

        if (start_seg < dest_seg) {
            for (int seg = start_seg; seg < dest_seg; seg++) {
                shared_data->seat_allocation[train_idx][seat_id][seg] = 0;
                shared_data->seats[train_idx][seg] += 1;
            }
        } else {
            for (int seg = start_seg - 1; seg >= dest_seg; seg--) {
                shared_data->seat_allocation[train_idx][seat_id][seg] = 0;
                shared_data->seats[train_idx][seg] += 1;
            }
        }
    }
}

void partial_cancel(BookingRecord *record, int num_to_cancel) {
    // 理論上呼叫前就確定 num_to_cancel <= record->tickets
    if (num_to_cancel <= 0 || num_to_cancel > record->tickets) {
        return;
    }

    int train_idx = record->train_index;
    int start_seg = record->start_index;
    int dest_seg  = record->dest_index;

    // 例如從後面開始釋放
    int start_idx = record->tickets - num_to_cancel;
    for (int i = start_idx; i < record->tickets; i++) {
        int seat_id = record->seat_numbers[i];

        if (start_seg < dest_seg) {
            for (int seg = start_seg; seg < dest_seg; seg++) {
                shared_data->seat_allocation[train_idx][seat_id][seg] = 0;
                shared_data->seats[train_idx][seg] += 1;
            }
        } else {
            for (int seg = start_seg - 1; seg >= dest_seg; seg--) {
                shared_data->seat_allocation[train_idx][seat_id][seg] = 0;
                shared_data->seats[train_idx][seg] += 1;
            }
        }
    }

    // 更新 tickets (扣除部分)
    record->tickets -= num_to_cancel;

    // 也把後面退掉的那幾個 seat_numbers 設為 -1 或 0 表示無效
    for (int i = 0; i < num_to_cancel; i++) {
        record->seat_numbers[record->tickets + i] = -1;
    }
}

int cancel_order(const char *id, 
                 int train_index, 
                 int start_index, 
                 int dest_index, 
                 int cancel_num) 
{
    semop(sem_id, &p_op, 1); // 取得鎖

    // (A) 統計同一 (id, train_index, start_index, dest_index) 總共可退的票數
    int total_tickets_can_cancel = 0;
    for (int i = 0; i < shared_data->booking_count; i++) {
        BookingRecord *rec = &shared_data->bookings[i];
        if ((strcmp(rec->id, id) == 0) &&
            rec->train_index == train_index &&
            rec->start_index == start_index &&
            rec->dest_index == dest_index)
        {
            total_tickets_can_cancel += rec->tickets;
        }
    }

    // (B) 若總可退數 < cancel_num => 直接失敗
    if (total_tickets_can_cancel < cancel_num) {
        semop(sem_id, &v_op, 1);
        return -1;
    }

    // (C) 逐筆退，直到把 cancel_num 退完
    int remain_to_cancel = cancel_num;
    int i = 0;
    while (i < shared_data->booking_count && remain_to_cancel > 0) {
        BookingRecord *rec = &shared_data->bookings[i];

        // 只針對同一 (id, train_index, start_index, dest_index)
        if ((strcmp(rec->id, id) == 0) &&
            rec->train_index == train_index &&
            rec->start_index == start_index &&
            rec->dest_index == dest_index)
        {
            // 若這筆訂單的票數 <= 要退的 => 全退
            if (rec->tickets <= remain_to_cancel) {
                free_seats_for_record(rec);  // 釋放這筆的所有座位
                remain_to_cancel -= rec->tickets;

                // 從 booking[] 移除 (最後一筆覆蓋)
                shared_data->booking_count--;
                if (i != shared_data->booking_count) {
                    shared_data->bookings[i] = shared_data->bookings[shared_data->booking_count];
                }
                // 不 i++，因為 i 位置現在是新的訂單
            } else {
                // 只退部分
                partial_cancel(rec, remain_to_cancel);
                remain_to_cancel = 0; // 退完
                i++;
            }
        } else {
            i++;
        }
    }

    semop(sem_id, &v_op, 1); // 釋放鎖
    return 0; // 成功
}




void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE];
    int bytes_read;
    pid_t pid = fork();
    if (pid == 0) {
        while ((bytes_read = recv(client_sock, buffer, BUFFER_SIZE, 0)) > 0) {
            buffer[bytes_read] = '\0';
            if (strncmp(buffer, "check_schedule", 14) == 0) {
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
                        // int remaining_seats = calculate_remaining_seats(i, start_index, farest_index);
                        // char temp[512], time[20], time2[20];
                        // encode_time(&shared_data->schedule[i][farest_index], time);
                        // encode_time(&shared_data->schedule[i][start_index], time2);
                        // sprintf(temp, "Farest Train %d from %s to %s (Seats: %d) at %s to %s\n",
                        //         i, line[start_index], line[farest_index], remaining_seats, time2,time);
                        // strcat(response, temp);
                        // break;
                        if (farest_index == start_index) {
                            char temp[512];
                            sprintf(temp, "No train available!");
                            strcat(response, temp);
                            break;
                        } else{
                            int remaining_seats = calculate_remaining_seats(i, start_index, farest_index);
                            char temp[512], time[20], time2[20];
                            encode_time(&shared_data->schedule[i][farest_index], time);
                            encode_time(&shared_data->schedule[i][start_index], time2);
                            sprintf(temp, "Farest Train %d from %s to %s (Seats: %d) at %s to %s\n",
                                    i, line[start_index], line[farest_index], remaining_seats, time2,time);
                            strcat(response, temp);
                            break;
                        }
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

                // 暫存合併後的結果 (假設上限 50 組)
                MergedBooking merged[50];
                memset(merged, 0, sizeof(merged));  // 全部清為 0

                // 遍歷所有訂單
                for (int i = 0; i < shared_data->booking_count; i++) {
                    BookingRecord *record = &shared_data->bookings[i];
                    if (strcmp(record->id, id) == 0) {
                        found = 1;

                        // 嘗試找到相同 (train_index, start_index, dest_index) 的項目
                        int merged_index = -1;
                        for (int m = 0; m < 50; m++) {
                            if (merged[m].used == 1 &&
                                merged[m].train_index == record->train_index &&
                                merged[m].start_index == record->start_index &&
                                merged[m].dest_index == record->dest_index) {
                                // 找到了
                                merged_index = m;
                                break;
                            }
                        }

                        // 如果沒找到 => 開一筆新的
                        if (merged_index == -1) {
                            for (int m = 0; m < 50; m++) {
                                if (merged[m].used == 0) {
                                    merged[m].used = 1;
                                    merged[m].train_index = record->train_index;
                                    merged[m].start_index = record->start_index;
                                    merged[m].dest_index = record->dest_index;
                                    merged[m].total_tickets = 0;
                                    merged[m].seat_count = 0;
                                    merged_index = m;
                                    break;
                                }
                            }
                        }

                        // 開始合併 
                        // (假設 merged_index 一定找到)
                        merged[merged_index].total_tickets += record->tickets;
                        // 把 record->seat_numbers[] append 到 merged[].seat_numbers 裡
                        for (int j = 0; j < record->tickets; j++) {
                            merged[merged_index].seat_numbers[ merged[merged_index].seat_count ] 
                                = record->seat_numbers[j];
                            merged[merged_index].seat_count++;
                        }
                    }
                }

                // 解鎖
                semop(sem_id, &v_op, 1);

                if (!found) {
                    strcpy(response, "No bookings found for this ID.\n");
                } else {
                    // 把 merged[] 的結果組合在 response
                    for (int m = 0; m < 50; m++) {
                        if (merged[m].used == 1) {
                            char booking_info[2048];
                            
                            // 先把座位號們寫成字串
                            char seat_list[1000] = "";
                            for (int s = 0; s < merged[m].seat_count; s++) {
                                char seat_buf[16];
                                sprintf(seat_buf, "%d", merged[m].seat_numbers[s]);
                                if (s > 0) strcat(seat_list, ", ");
                                strcat(seat_list, seat_buf);
                            }

                            sprintf(booking_info,
                                    "Train %d from %s to %s. Tickets: %d. Seats: %s\n",
                                    merged[m].train_index,
                                    line[ merged[m].start_index ],
                                    line[ merged[m].dest_index ],
                                    merged[m].total_tickets,
                                    seat_list
                            );
                            strcat(response, booking_info);
                        }
                    }
                }

                // 傳回給客戶端
                send(client_sock, response, strlen(response), 0);
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
                char id[20], start[20], dest[20];
                int train_index, cancel_num;

                // 新格式: cancel_order <ID> <train_index> <start> <dest> <cancel_num>
                sscanf(buffer + 13, "%s %d %s %s %d", id, &train_index, start, dest, &cancel_num);

                printf("[DEBUG] cancel_order: ID=%s, train_index=%d, start=%s, dest=%s, cancel_num=%d\n",
                    id, train_index, start, dest, cancel_num);

                // 解析站名 => 取得起迄站索引
                int start_index = handle_point(start);
                int dest_index  = handle_point(dest);

                // 如果解析失敗 => 回傳錯誤
                if (start_index == -1 || dest_index == -1) {
                    char msg[128];
                    sprintf(msg, "Cancel order fail. Invalid station name.\n");
                    send(client_sock, msg, strlen(msg), 0);
                    continue;
                }

                // 呼叫新的 cancel_order
                if (cancel_order(id, train_index, start_index, dest_index, cancel_num) == 0) {
                    // 成功
                    char msg[128];
                    sprintf(msg, "Cancel order success. ID=%s, Train=%d, Start=%s, Dest=%s, Cancel=%d\n",
                            id, train_index, start, dest, cancel_num);
                    send(client_sock, msg, strlen(msg), 0);
                } else {
                    // 失敗
                    char msg[128];
                    sprintf(msg, "Cancel order fail. ID=%s, Train=%d, Start=%s, Dest=%s, Cancel=%d\n",
                            id, train_index, start, dest, cancel_num);
                    send(client_sock, msg, strlen(msg), 0);
                }
            } else if (strncmp(buffer, "manual_book_ticket", 18) == 0) {
                int train_index, start_index, dest_index, tickets;
                char id[20];
                char start[20], dest[20];
                
                // 解析客戶端傳入的指令
                sscanf(buffer + 19, "%d %s %s %d %s", &train_index, start, dest, &tickets, id);
                printf("\nreceive %d %s %s %d %s\n", train_index, start, dest, tickets, id);
                fflush(stdout);

                start_index = handle_point(start);
                dest_index = handle_point(dest);

                if (start_index == -1 || dest_index == -1) {
                    const char *error_msg = "Invalid station name.\n";
                    send(client_sock, error_msg, strlen(error_msg), 0);
                    continue;
                }

                char seat_status[BUFFER_SIZE] = "";
                
                // 傳回區段與座位狀態
                if (start_index < dest_index) {
                    for (int i = start_index; i < dest_index; i++) {
                        char segment_status[100] = "";
                        sprintf(segment_status, "Segment %s to %s:\n", line[i], line[i + 1]);
                        strcat(seat_status, segment_status);
                        for (int j = 0; j < SEAT_AMOUNT; j++) {
                            char seat_info[20];
                            sprintf(seat_info, "Seat %d: %d  ", j, shared_data->seat_allocation[train_index][j][i]);
                            strcat(seat_status, seat_info);
                            if ((j + 1) % 5 == 0) strcat(seat_status, "\n");
                        }
                        strcat(seat_status, "\n");
                    }
                } else {
                    for (int i = start_index-1; i >= dest_index; i--) {
                        char segment_status[100] = "";
                        sprintf(segment_status, "Segment %s to %s:\n", line[i], line[i - 1]);
                        strcat(seat_status, segment_status);
                        for (int j = 0; j < SEAT_AMOUNT; j++) {
                            char seat_info[20];
                            sprintf(seat_info, "Seat %d: %d  ", j, shared_data->seat_allocation[train_index][j][i]);
                            strcat(seat_status, seat_info);
                            if ((j + 1) % 5 == 0) strcat(seat_status, "\n");
                        }
                        strcat(seat_status, "\n");
                    }
                }
                printf("%s\n", seat_status);
                fflush(stdout);
                // 傳送座位狀態到客戶端
                send(client_sock, seat_status, strlen(seat_status), 0);

                // 接收客戶端回傳的座位選擇
                char selected_seats[BUFFER_SIZE];
                recv(client_sock, selected_seats, BUFFER_SIZE - 1, 0);
                printf("%s\n", selected_seats);
                fflush(stdout);
                // 更新共享記憶體中的座位狀態
                int seat_number, counter;
                counter = 0;
                char *token = strtok(selected_seats, " ");
                while (token != NULL && counter < tickets) {
                    seat_number = atoi(token);
                    if (start_index < dest_index){
                        for (int i = start_index; i < dest_index; i++) {
                            shared_data->seat_allocation[train_index][seat_number][i] = 1;
                            shared_data->seats[train_index][i]--;
                        }
                    } 
                    else {
                        for (int i = start_index - 1; i >= dest_index; i--) {
                            shared_data->seat_allocation[train_index][seat_number][i] = 1;
                            shared_data->seats[train_index][i]--;
                        }                        
                    }
                    token = strtok(NULL, " ");
                    counter++;
                }

                // 傳回訂票確認
                char confirmation[BUFFER_SIZE];
                sprintf(confirmation, "Manual booking confirmed for Train %d from %s to %s. ID: %s\n",
                        train_index, line[start_index], line[dest_index], id);
                printf("send back");
                fflush(stdout);
                send(client_sock, confirmation, strlen(confirmation), 0);
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
    key_t sem_key = ftok("train_subserver.c", 2);
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
