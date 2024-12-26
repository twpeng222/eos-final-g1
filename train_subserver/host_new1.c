#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define BUFFER_SIZE 512
#define MAX_CLIENTS 10

// 定義服務端地址
#define HIGH_SPEED_IP "192.168.56.101"
#define HIGH_SPEED_PORT 8881
#define TRAIN_IP "192.168.56.101"
#define TRAIN_PORT 8889

// 可達站點
const char Points[8][20] = {"Taitung", "Hualien", "Yilan", "Taipei", "Taoyuan", "Taichung", "Tainan", "Kaohsiung"};

// 查詢字串在陣列中的索引，如果找不到，回傳 -1
int findIndex(const char Points[][20], int size, const char *target) {
    for (int i = 0; i < size; i++) {
        if (strcmp(Points[i], target) == 0) {
            return i; // 找到時回傳索引
        }
    }
    return -1; // 找不到時回傳 -1
}

// 客戶端資料結構
typedef struct {
    int client_sock;
    int ht_sock;
    int tr_sock;
} client_data_t;

// check回傳的資料結構
typedef struct {
    int check_state;
    int train_no;
    int remaining_seats;
    char farest_dest[20];
    char start[20];
    char depart_time[20];
    char arrive_time[20];
} check_data_t;

// book回傳的資料結構
typedef struct {
    int book_state;
    int train_no;
    int tickets;
    char tickets_info[100];
    char start[20];
    char dest[20];
} book_data_t;

// 連接到服務端的輔助函數
int connect_to_server(const char* ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Could not create socket");
        return -1;
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        perror("Connection failed");
        close(sock);
        return -1;
    }

    return sock;
}

// 處理查詢票
void check(check_data_t* check_data, int sock, int start_idx, int dest_idx, const char* time, int amount){
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    sprintf(response, "check_schedule %s %s %s %d", Points[start_idx], Points[dest_idx], time, amount);
    write(sock, response, BUFFER_SIZE);
    read(sock, buffer, BUFFER_SIZE);
    if(strncmp(buffer, "Farest", 6) == 0) {
        sscanf(buffer, "Farest Train %d from %s to %s (Seats: %d) at %s to %s\n",
           &check_data->train_no, check_data->start, check_data->farest_dest, &check_data->remaining_seats, check_data->arrive_time, check_data->depart_time);
        if (strcmp(check_data->farest_dest, Points[dest_idx]) == 0) {
            check_data->check_state = 1;
        } else {
            check_data->check_state = 2;
        }
    } else {
        check_data->check_state = 0;
    }
}

// 處理訂票
void book(book_data_t* book_data, int sock, int train_no, int start_idx, int dest_idx, int amount, const char* ID){
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    sprintf(response, "book_ticket %d %s %s %d %s %d", train_no, Points[start_idx], Points[dest_idx], amount, ID, 1);
    write(sock, response, BUFFER_SIZE);
    read(sock, buffer, BUFFER_SIZE);
    if (strncmp(buffer, "Booking confirmed", 17) == 0) {
        sscanf(buffer, "Booking confirmed for Train %d from %s to %s. Tickets: %d. ID: %s\n",
           &book_data->train_no, book_data->start, book_data->dest, &book_data->tickets, book_data->tickets_info);
        book_data->book_state = 1;
    } else {
        book_data->book_state = 0;
    }
}

void check_and_book(int ht_sock, int tr_sock, int client_sock, int start_idx, int dest_idx, const char* time, int amount, const char* ID) {
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    int direction = (start_idx < dest_idx);
    int not_cross = (start_idx<3 && dest_idx<3) || (start_idx>4 && dest_idx>4);
    
    check_data_t* train_check_data = malloc(sizeof(check_data_t));
    book_data_t* train_book_data = malloc(sizeof(book_data_t));
    check_data_t* high_check_data = malloc(sizeof(check_data_t));
    book_data_t* high_book_data = malloc(sizeof(book_data_t));

    // 處理只有 train 的情況
    if (start_idx<3 && dest_idx<3) {
        check(train_check_data, tr_sock, start_idx, dest_idx, time, amount);
        if (train_check_data->check_state == 1) {
            book(train_book_data, tr_sock, train_check_data->train_no, start_idx, dest_idx, amount, ID);
            snprintf(response, BUFFER_SIZE,
                     "Booking Confirmed!\n"
                     "Train No: %d\nFrom: %s\nTo: %s\nTickets: %d\n",
                     train_book_data->train_no, Points[start_idx], Points[dest_idx], amount);
            write(client_sock, response, BUFFER_SIZE);
        } else {
            snprintf(response, BUFFER_SIZE, "No train available from %s to %s.\n", Points[start_idx], Points[dest_idx]);
            write(client_sock, response, BUFFER_SIZE);
        }
        free(train_check_data);
        free(train_book_data);
        free(high_check_data);
        free(high_book_data);
        return;
    }

    // 處理只有 high-railway 的情況
    if (start_idx>4 && dest_idx>4) {
        check(high_check_data, ht_sock, start_idx, dest_idx, time, amount);
        if (high_check_data->check_state == 1) {
            book(high_book_data, ht_sock, high_check_data->train_no, start_idx, dest_idx, amount, ID);
            snprintf(response, BUFFER_SIZE,
                     "Booking Confirmed!\n"
                     "High No: %d\nFrom: %s\nTo: %s\nTickets: %d\n",
                     high_book_data->train_no, Points[start_idx], Points[dest_idx], amount);
            write(client_sock, response, BUFFER_SIZE);
        } else {
            snprintf(response, BUFFER_SIZE, "No HSR available from %s to %s.\n", Points[start_idx], Points[dest_idx]);
            write(client_sock, response, BUFFER_SIZE);
        }
        free(train_check_data);
        free(train_book_data);
        free(high_check_data);
        free(high_book_data);
        return;
    }

    // 處理北到南的情況
    if (start_idx<dest_idx && start_idx<3) {
        // 處理 (0-2) ~ 的情況
        check(train_check_data, tr_sock, start_idx, 4, time, amount);
        if (train_check_data->check_state == 0) {
            snprintf(response, BUFFER_SIZE, "No train available from %s to %s.\n", Points[start_idx], Points[dest_idx]);
            write(client_sock, response, BUFFER_SIZE);
            free(train_check_data);
            free(train_book_data);
            free(high_check_data);
            free(high_book_data);
            return;
        }

        int train_destination = findIndex(Points, 8, train_check_data->farest_dest);

        // 處理 (0-2) ~ (3-4)
        if (train_destination < 3) {
            snprintf(response, BUFFER_SIZE, "No train available from %s to %s.\n", Points[start_idx], Points[dest_idx]);
            write(client_sock, response, BUFFER_SIZE);
        } else {
            if (train_destination == dest_idx) {
                book(train_book_data, tr_sock, train_check_data->train_no, start_idx, train_destination, amount, ID);
                snprintf(response, BUFFER_SIZE,
                         "Booking Confirmed!\n"
                         "Train No: %d\nFrom: %s\nTo: %s\nTickets: %d\n",
                         train_book_data->train_no, Points[start_idx], Points[dest_idx], amount);
                write(client_sock, response, BUFFER_SIZE);
            } else {
                // 處理 (0-2) ~ (5~7)
                check(high_check_data, ht_sock, train_destination, dest_idx, train_check_data->arrive_time, amount);
                if (high_check_data->check_state == 0) {
                    snprintf(response, BUFFER_SIZE, "No HSR available from %s to %s.\n", Points[start_idx], Points[dest_idx]);
                    write(client_sock, response, BUFFER_SIZE);
                } else {
                    int high_destination = findIndex(Points, 8, high_check_data->farest_dest);
                    if (high_destination == dest_idx) {
                        book(train_book_data, tr_sock, train_check_data->train_no, start_idx, train_destination, amount, ID);
                        book(high_book_data, ht_sock, high_check_data->train_no, train_destination, high_destination, amount, ID);
                        snprintf(response, BUFFER_SIZE,
                             "Booking Confirmed!\n"
                             "First Leg:\n  Train No: %d\n  From: %s\n  To: %s\n  Tickets: %d\n"
                             "Second Leg:\n  High No: %d\n  From: %s\n  To: %s\n  Tickets: %d\n",
                             train_book_data->train_no, Points[start_idx], Points[train_destination], amount,
                             high_book_data->train_no, Points[train_destination], Points[dest_idx], amount);
                    }
                }
            }
        }
        free(train_check_data);
        free(train_book_data);
        free(high_check_data);
        free(high_book_data);
        return;
    } else {
        // 處理 (3-4) ~ (4-7)
        check(high_check_data, ht_sock, start_idx, dest_idx, time, amount);
        if (high_check_data->check_state == 1) {
            book(high_book_data, ht_sock, high_check_data->train_no, start_idx, dest_idx, amount, ID);
            snprintf(response, BUFFER_SIZE,
                     "Booking Confirmed!\n"
                     "High No: %d\nFrom: %s\nTo: %s\nTickets: %d\n",
                     high_book_data->train_no, Points[start_idx], Points[dest_idx], amount);
            write(client_sock, response, BUFFER_SIZE);
        } else if (high_check_data->check_state == 0 && start_idx == 3) {
            check(train_check_data, tr_sock, start_idx, 4, time, amount);
            if (train_check_data->check_state == 0) {
                sprintf(response, "No train/HSR available from %s to %s.\n", Points[start_idx], Points[dest_idx]);
                write(client_sock, response, BUFFER_SIZE);
            } else {
                if (dest_idx == 4) {
                    book(train_book_data, tr_sock, train_check_data->train_no, start_idx, 4, amount, ID);
                    snprintf(response, BUFFER_SIZE,
                         "Booking Confirmed!\n"
                         "Train No: %d\nFrom: %s\nTo: %s\nTickets: %d\n",
                         train_book_data->train_no, Points[start_idx], Points[dest_idx], amount);
                    write(client_sock, response, BUFFER_SIZE);
                } else {
                    check(high_check_data, ht_sock, 4, dest_idx, train_check_data->arrive_time, amount);
                    if (high_check_data->check_state == 1) {
                        book(train_book_data, tr_sock, train_check_data->train_no, start_idx, 4, amount, ID);
                        book(high_book_data, ht_sock, high_check_data->train_no, 4, dest_idx, amount, ID);
                        snprintf(response, BUFFER_SIZE,
                             "Booking Confirmed!\n"
                             "First Leg:\n  Train No: %d\n  From: %s\n  To: %s\n  Tickets: %d\n"
                             "Second Leg:\n  High No: %d\n  From: %s\n  To: %s\n  Tickets: %d\n",
                             train_book_data->train_no, Points[start_idx], Points[4], amount,
                             high_book_data->train_no, Points[4], Points[dest_idx], amount);
                        write(client_sock, response, BUFFER_SIZE);
                    } else {
                        snprintf(response, BUFFER_SIZE, "No HSR available from %s to %s.\n", Points[start_idx], Points[dest_idx]);
                        write(client_sock, response, BUFFER_SIZE);
                    }
                }
            }
        } else {
            snprintf(response, BUFFER_SIZE, "No HSR available from %s to %s.\n", Points[start_idx], Points[dest_idx]);
            write(client_sock, response, BUFFER_SIZE);
        }
        free(train_check_data);
        free(train_book_data);
        free(high_check_data);
        free(high_book_data);
        return;
    }

    // 處理南到北的情況
    if (start_idx>dest_idx && start_idx>4) {
        // 處理 (5~7) ~ 的情況
        check(high_check_data, ht_sock, start_idx, 3, time, amount);
        if (high_check_data->check_state == 0) {
            snprintf(response, BUFFER_SIZE, "No HSR available from %s to %s.\n", Points[start_idx], Points[dest_idx]);
            write(client_sock, response, BUFFER_SIZE);
            free(train_check_data);
            free(train_book_data);
            free(high_check_data);
            free(high_book_data);
            return;
        }

        int high_destination = findIndex(Points, 8, high_check_data->farest_dest);

        // 處理 (5~7) ~ (3-4)
        if (high_destination > 4) {
            snprintf(response, BUFFER_SIZE, "No HSR available from %s to %s.\n", Points[start_idx], Points[dest_idx]);
            write(client_sock, response, BUFFER_SIZE);
        } else {
            if (high_destination == dest_idx) {
                book(high_book_data, ht_sock, high_check_data->train_no, start_idx, high_destination, amount, ID);
                snprintf(response, BUFFER_SIZE,
                     "Booking Confirmed!\n"
                     "High No: %d\nFrom: %s\nTo: %s\nTickets: %d\n",
                     high_book_data->train_no, Points[start_idx], Points[dest_idx], amount);
                write(client_sock, response, BUFFER_SIZE);
            } else {
                // 處理 (5~7) ~ (0-2)
                check(train_check_data, tr_sock, high_destination, dest_idx, high_check_data->arrive_time, amount);
                if (train_check_data->check_state == 0) {
                    snprintf(response, BUFFER_SIZE, "No train available from %s to %s.\n", Points[start_idx], Points[dest_idx]);
                    write(client_sock, response, BUFFER_SIZE);
                } else {
                    int train_destination = findIndex(Points, 8, train_check_data->farest_dest);
                    if (train_destination == dest_idx) {
                        book(high_book_data, ht_sock, high_check_data->train_no, start_idx, high_destination, amount, ID);
                        book(train_book_data, tr_sock, train_check_data->train_no, high_destination, train_destination, amount, ID);
                        snprintf(response, BUFFER_SIZE,
                             "Booking Confirmed!\n"
                             "First Leg:\n  High No: %d\n  From: %s\n  To: %s\n  Tickets: %d\n"
                             "Second Leg:\n  Train No: %d\n  From: %s\n  To: %s\n  Tickets: %d\n",
                             high_book_data->train_no, Points[start_idx], Points[high_destination], amount,
                             train_book_data->train_no, Points[high_destination], Points[dest_idx], amount);
                    }
                }
            }
        }
        free(train_check_data);
        free(train_book_data);
        free(high_check_data);
        free(high_book_data);
        return;
    } else {
        // 處理 (3-4) ~ (0-2)
        check(train_check_data, tr_sock, start_idx, dest_idx, time, amount);
        if (train_check_data->check_state == 1) {
            book(train_book_data, tr_sock, train_check_data->train_no, start_idx, dest_idx, amount, ID);
            snprintf(response, BUFFER_SIZE,
                         "Booking Confirmed!\n"
                         "Train No: %d\nFrom: %s\nTo: %s\nTickets: %d\n",
                         train_book_data->train_no, Points[start_idx], Points[dest_idx], amount);
            write(client_sock, response, BUFFER_SIZE);
        } else if (train_check_data->check_state == 0 && start_idx == 4) {
            check(high_check_data, ht_sock, 4, 3, time, amount);
            if (high_check_data->check_state == 0) {
                sprintf(response, "No HSR/train available from %s to %s.\n", Points[start_idx], Points[dest_idx]);
                write(client_sock, response, BUFFER_SIZE);
            } else {
                if (dest_idx == 3) {
                    book(high_book_data, ht_sock, high_check_data->train_no, 4, 3, amount, ID);
                    snprintf(response, BUFFER_SIZE,
                     "Booking Confirmed!\n"
                     "High No: %d\nFrom: %s\nTo: %s\nTickets: %d\n",
                     high_book_data->train_no, Points[start_idx], Points[dest_idx], amount);
                    write(client_sock, response, BUFFER_SIZE);
                } else {
                    check(train_check_data, tr_sock, 3, dest_idx, high_check_data->arrive_time, amount);
                    if (train_check_data->check_state == 1) {
                        book(high_book_data, ht_sock, high_check_data->train_no, 3, 4, amount, ID);
                        book(train_book_data, tr_sock, train_check_data->train_no, 4, dest_idx, amount, ID);
                        snprintf(response, BUFFER_SIZE,
                             "Booking Confirmed!\n"
                             "First Leg:\n  High No: %d\n  From: %s\n  To: %s\n  Tickets: %d\n"
                             "Second Leg:\n  Train No: %d\n  From: %s\n  To: %s\n  Tickets: %d\n",
                             high_book_data->train_no, Points[3], Points[4], amount,
                             train_book_data->train_no, Points[4], Points[dest_idx], amount);
                        write(client_sock, response, BUFFER_SIZE);
                    } else {
                        snprintf(response, BUFFER_SIZE, "No train available from %s to %s.\n", Points[start_idx], Points[dest_idx]);
                        write(client_sock, response, BUFFER_SIZE);
                    }
                }
            }
        } else {
            snprintf(response, BUFFER_SIZE, "No train available from %s to %s.\n", Points[start_idx], Points[dest_idx]);
            write(client_sock, response, BUFFER_SIZE);
        }
        free(train_check_data);
        free(train_book_data);
        free(high_check_data);
        free(high_book_data);
        return;
    }
    free(train_check_data);
    free(train_book_data);
    free(high_check_data);
    free(high_book_data);
}

// 客戶端執行緒函數
void* handle_client(void* client_data_ptr) {
    client_data_t* data = (client_data_t*)client_data_ptr;
    int client_sock = data->client_sock;
    int ht_sock = data->ht_sock;
    int tr_sock = data->tr_sock;

    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char start[20], dest[20], time[20], ID[20];
    int start_idx, dest_idx;
    int amount;

    // 接收客戶端指令
    while (1) {
        // receive client request
        int read_size = read(client_sock, buffer, BUFFER_SIZE);
        if (read_size <= 0) {
            printf("Client disconnected\n");
            break;
        }
        if (strcmp(buffer, "quit") == 0){
            break;
        }

        printf("client request : %s", buffer);

        if (sscanf(buffer, "%d %d %s %d %s", &start_idx, &dest_idx, time, &amount, ID) == 5) {
            check_and_book(ht_sock, tr_sock, client_sock, start_idx, dest_idx, time, amount);
        } else {
            sprintf(response, "Invalid command format. Please enter your book request again:\n");
            write(client_sock, response, BUFFER_SIZE);
        }
    }

    // 清理資源
    close(client_sock);
    close(ht_sock);
    close(tr_sock);
    free(data);

    return NULL;
}

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server, client;
    int client_count = 0;

    // 創建主伺服器 socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("Could not create server socket");
        return 1;
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(7777);  // HOST 的監聽埠
    server.sin_addr.s_addr = inet_addr("127.0.0.1");

    // 綁定 socket
    if (bind(server_sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        perror("Bind failed");
        return 1;
    }

    // 開始監聽
    listen(server_sock, MAX_CLIENTS);
    printf("Server listening on port 12345...\n");

    while (1) {
        socklen_t client_size = sizeof(client);
        client_sock = accept(server_sock, (struct sockaddr*)&client, &client_size);
        if (client_sock < 0) {
            perror("Accept failed");
            continue;
        }

        printf("Client connected\n");

        // 動態分配 client_data
        client_data_t* client_data = malloc(sizeof(client_data_t));
        if (!client_data) {
            perror("Could not allocate memory for client data");
            close(client_sock);
            continue;
        }

        // 建立高鐵和台鐵的連接
        client_data->ht_sock = connect_to_server(HIGH_SPEED_IP, HIGH_SPEED_PORT);
        client_data->tr_sock = connect_to_server(TRAIN_IP, TRAIN_PORT);
        if (client_data->ht_sock < 0 || client_data->tr_sock < 0) {
            perror("Failed to connect to train services");
            free(client_data);
            close(client_sock);
            continue;
        }

        // 初始化客戶端資料
        client_data->client_sock = client_sock;

        // 創建執行緒處理該客戶端
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, (void*)client_data) != 0) {
            perror("Failed to create thread");
            free(client_data);
            close(client_sock);
            close(client_data->ht_sock);
            close(client_data->tr_sock);
            continue;
        }
        pthread_detach(tid); // 分離執行緒，讓其自行管理生命週期
    }

    // 關閉伺服器 socket
    close(server_sock);
    return 0;
}