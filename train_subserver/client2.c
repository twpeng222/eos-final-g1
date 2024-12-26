#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ncurses.h>
#include <signal.h>

#define BUFFER_SIZE 512
#define TIMER 10

const char *stations[] = {"Taitung", "Hualien", "Yilan", "Taipei", "Taoyuan", "Taichung", "Tainan", "Kaohsiung"};
#define NUM_STATIONS (sizeof(stations) / sizeof(stations[0]))

int sock;

void handle_alarm(int sig) {
    if (sig == SIGALRM) {
        endwin();
        close(sock);
        printf("No input for %d seconds. Connection terminated.\n",TIMER);
        exit(EXIT_FAILURE);
    }
}

void draw_menu(WINDOW *menu_win, int highlight, const char **choices, int n_choices) {
    box(menu_win, 0, 0);
    for (int i = 0; i < n_choices; ++i) {
        if (i == highlight)
            wattron(menu_win, A_REVERSE);
        mvwprintw(menu_win, i + 1, 2, "%s", choices[i]);
        wattroff(menu_win, A_REVERSE);
    }
    wrefresh(menu_win);
}

int select_option(const char *prompt, const char **choices, int n_choices) {
    int highlight = 0;
    int choice;
    WINDOW *menu_win = newwin(n_choices + 2, 40, (LINES - n_choices) / 2, (COLS - 40) / 2);
    keypad(menu_win, TRUE);

    while (1) {
        mvprintw(LINES - 3, 0, "%s", prompt);
        refresh();
        draw_menu(menu_win, highlight, choices, n_choices);

        alarm(20); // Reset the alarm
        choice = wgetch(menu_win);
        switch (choice) {
            case KEY_UP:
                highlight = (highlight - 1 + n_choices) % n_choices;
                break;
            case KEY_DOWN:
                highlight = (highlight + 1) % n_choices;
                break;
            case 10: // Enter key
                delwin(menu_win);
                alarm(0); // Cancel the alarm
                return highlight;
        }
    }
}

void get_input(const char *prompt, char *input, int max_length) {
    echo();
    mvprintw(LINES - 3, 0, "%s", prompt);
    clrtoeol();
    refresh();
    alarm(20); // Reset the alarm
    mvgetnstr(LINES - 3, strlen(prompt), input, max_length);
    alarm(0); // Cancel the alarm
    noecho();
    mvprintw(LINES - 3, 0, "Press ENTER to continue...");
    refresh();
    getch();
    clear();
    refresh();
}

void get_time_input(char *time_input) {
    char year[5], month[3], day[3], hour[3], minute[3];

    get_input("Enter year (YYYY): ", year, 5);
    get_input("Enter month (MM): ", month, 3);
    get_input("Enter day (DD): ", day, 3);
    get_input("Enter hour (HH): ", hour, 3);
    get_input("Enter minute (MM): ", minute, 3);

    snprintf(time_input, 20, "%s/%s/%s/%s:%s", year, month, day, hour, minute);
}

void connect_to_server(const char *ip, int port, int *sock) {
    struct sockaddr_in server_addr;

    *sock = socket(AF_INET, SOCK_STREAM, 0);
    if (*sock == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);

    if (connect(*sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect failed");
        close(*sock);
        exit(EXIT_FAILURE);
    }
}

void handle_response(int sock) {
    char response[BUFFER_SIZE];
    int bytes_read = recv(sock, response, BUFFER_SIZE - 1, 0);
    if (bytes_read > 0) {
        response[bytes_read] = '\0';
        mvprintw(LINES - 2, 0, "%s", response);
    } else {
        mvprintw(LINES - 2, 0, "No response from server.");
    }
    clrtoeol();
    refresh();
    mvprintw(LINES - 3, 0, "Press ENTER to continue...");
    refresh();
    getch();
    clear();
    refresh();
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

    signal(SIGALRM, handle_alarm); // Set up the signal handler

    connect_to_server(server_ip, port, &sock);

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    const char *choices[] = {
        "Check Train Schedule",
        "Book Tickets",
        "Check Orders",
        "Cancel Order",
        "Exit"
    };
    int n_choices = sizeof(choices) / sizeof(char *);
    int highlight = 0;
    int choice;

    WINDOW *menu_win = newwin(n_choices + 2, 40, (LINES - n_choices) / 2, (COLS - 40) / 2);
    keypad(menu_win, TRUE);

    while (1) {
        draw_menu(menu_win, highlight, choices, n_choices);

        alarm(TIMER); // Reset the alarm
        choice = wgetch(menu_win);
        switch (choice) {
            case KEY_UP:
                highlight = (highlight - 1 + n_choices) % n_choices;
                break;
            case KEY_DOWN:
                highlight = (highlight + 1) % n_choices;
                break;
            case 10: // Enter key
                alarm(0); // Cancel the alarm
                if (strcmp(choices[highlight], "Exit") == 0) {
                    endwin();
                    close(sock);
                    exit(EXIT_SUCCESS);
                } else if (strcmp(choices[highlight], "Check Train Schedule") == 0) {
                    int start_index = select_option("Select start station:", stations, NUM_STATIONS);
                    int dest_index = select_option("Select destination station:", stations, NUM_STATIONS);
                    char time[20];
                    int tickets;

                    get_time_input(time);
                    char tickets_str[4];
                    get_input("Enter number of tickets: ", tickets_str, 4);
                    tickets = atoi(tickets_str);

                    char request[BUFFER_SIZE];
                    snprintf(request, BUFFER_SIZE, "check_schedule %s %s %s %d", stations[start_index], stations[dest_index], time, tickets);
                    send(sock, request, strlen(request), 0);
                    handle_response(sock);
                } else if (strcmp(choices[highlight], "Book Tickets") == 0) {
                    int start_index = select_option("Select start station:", stations, NUM_STATIONS);
                    int dest_index = select_option("Select destination station:", stations, NUM_STATIONS);
                    char id[20];
                    int train_index, tickets, contiguous;

                    char train_index_str[4];
                    get_input("Enter train index: ", train_index_str, 4);
                    train_index = atoi(train_index_str);
                    char tickets_str[4];
                    get_input("Enter number of tickets: ", tickets_str, 4);
                    tickets = atoi(tickets_str);
                    get_input("Enter your ID: ", id, 20);
                    char contiguous_str[4];
                    get_input("Contiguous seating (1 for yes, 0 for no): ", contiguous_str, 4);
                    contiguous = atoi(contiguous_str);

                    char request[BUFFER_SIZE];
                    snprintf(request, BUFFER_SIZE, "book_ticket %d %s %s %d %s %d", train_index, stations[start_index], stations[dest_index], tickets, id, contiguous);
                    send(sock, request, strlen(request), 0);
                    handle_response(sock);
                } else if (strcmp(choices[highlight], "Check Orders") == 0) {
                    char id[20];
                    get_input("Enter your ID: ", id, 20);

                    char request[BUFFER_SIZE];
                    snprintf(request, BUFFER_SIZE, "check_order %s", id);
                    send(sock, request, strlen(request), 0);
                    handle_response(sock);
                } else if (strcmp(choices[highlight], "Cancel Order") == 0) {
                    char id[20];
                    int start_index = select_option("Select start station:", stations, NUM_STATIONS);
                    int dest_index = select_option("Select destination station:", stations, NUM_STATIONS);
                    char train_index_str[4], tickets_str[4];
                    int train_index, tickets;

                    get_input("Enter your ID: ", id, 20);
                    get_input("Enter train index: ", train_index_str, 4);
                    train_index = atoi(train_index_str);
                    get_input("Enter number of tickets to cancel: ", tickets_str, 4);
                    tickets = atoi(tickets_str);

                    char request[BUFFER_SIZE];
                    snprintf(request, BUFFER_SIZE, "cancel_order %s %d %s %s %d", id, train_index, stations[start_index], stations[dest_index], tickets);
                    send(sock, request, strlen(request), 0);
                    handle_response(sock);
                }
                break;
        }
    }

    endwin();
    close(sock);
    return 0;
}
