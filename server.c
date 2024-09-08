#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#define DEFAULT_PORT 12345
#define DEFAULT_LOG_FILE "/tmp/lab2.log"
#define DEFAULT_ADDRESS "0.0.0.0"
#define QUEUE_SIZE 5
#define BUFFER_SIZE 1024
#define DEFAULT_WAIT 0

typedef struct Node {
    char data[BUFFER_SIZE];
    struct Node* next;
} Node;

Node* head = NULL;
Node* tail = NULL;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

int server_fd;
FILE *log_file;
int running = 1;  // Переменная для управления состоянием работы сервера
int wait_time = DEFAULT_WAIT;  // Время задержки обработки запроса

void handle_signal(int signal);
void handle_client(struct sockaddr_in* client_addr, char* buffer, int len);
void log_message(const char *message);
void enqueue(const char* data);
int dequeue(char* buffer);
int is_valid_number(const char *str);
int validate_port(int port);
int validate_ip(const char *ip);
int validate_log_file_path(const char *path);

void handle_signal(int signal) {
    switch (signal) {
        case SIGINT:
        case SIGTERM:
        case SIGQUIT:
            log_message("Received termination signal, shutting down server...");
            if (log_file) {
                fclose(log_file);
            }
            close(server_fd);
            exit(0);
            break;
        case SIGUSR1:
            log_message("Received SIGUSR1 signal, exiting...");
            if (log_file) {
                fclose(log_file);
            }
            close(server_fd);
            exit(0);
            break;
        default:
            break;
    }
}

void handle_client(struct sockaddr_in* client_addr, char* buffer, int len) {
    buffer[len] = '\0';
    log_message("Received request from client");

    if (strncmp(buffer, "PUT", 3) == 0) {
        log_message("Handling PUT request");
        if (is_valid_number(buffer + 4)) {
            enqueue(buffer + 4);  // Add the number after "PUT "
            sendto(server_fd, "OK\n", 3, 0, (struct sockaddr*)client_addr, sizeof(*client_addr));
        } else {
            sendto(server_fd, "ERROR 3\n", 8, 0, (struct sockaddr*)client_addr, sizeof(*client_addr));  // Invalid number
        }
    } else if (strncmp(buffer, "GET", 3) == 0) {
        log_message("Handling GET request");
        if (dequeue(buffer)) {
            sendto(server_fd, buffer, strlen(buffer), 0, (struct sockaddr*)client_addr, sizeof(*client_addr));
        } else {
            sendto(server_fd, "ERROR 1\n", 8, 0, (struct sockaddr*)client_addr, sizeof(*client_addr));  // Queue empty
        }
    } else if (strncmp(buffer, "EXIT", 4) == 0) {
        log_message("Received EXIT command, shutting down server...");
        sendto(server_fd, "Server is shutting down\n", 24, 0, (struct sockaddr*)client_addr, sizeof(*client_addr));
        running = 0;  // Устанавливаем переменную в 0 для завершения работы сервера
    } else {
        log_message("Invalid request received");
        sendto(server_fd, "ERROR 2\n", 8, 0, (struct sockaddr*)client_addr, sizeof(*client_addr));  // Invalid command
    }

    // Имитация работы путем задержки
    if (wait_time > 0) {
        sleep(wait_time);
    }
}

int is_valid_number(const char *str) {
    char *endptr;
    strtod(str, &endptr);
    return *endptr == '\0' || *endptr == '\n';
}

int validate_port(int port) {
    return port > 0 && port <= 65535;
}

int validate_ip(const char *ip) {
    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip, &(sa.sin_addr)) != 0;
}

int validate_log_file_path(const char *path) {
    FILE *file = fopen(path, "a");
    if (file) {
        fclose(file);
        return 1;
    }
    return 0;
}

void enqueue(const char* data) {
    pthread_mutex_lock(&queue_mutex);
    Node* new_node = (Node*)malloc(sizeof(Node));
    strncpy(new_node->data, data, BUFFER_SIZE);
    new_node->next = NULL;
    if (tail) {
        tail->next = new_node;
    }
    tail = new_node;
    if (!head) {
        head = tail;
    }
    pthread_mutex_unlock(&queue_mutex);
}

int dequeue(char* buffer) {
    pthread_mutex_lock(&queue_mutex);
    if (!head) {
        pthread_mutex_unlock(&queue_mutex);
        return 0;
    }
    Node* temp = head;
    strncpy(buffer, head->data, BUFFER_SIZE);
    head = head->next;
    if (!head) {
        tail = NULL;
    }
    free(temp);
    pthread_mutex_unlock(&queue_mutex);
    return 1;
}

void log_message(const char *message) {
    time_t now = time(NULL);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%d.%m.%Y %H:%M:%S", localtime(&now));

    if (log_file) {
        fprintf(log_file, "[%s] %s\n", timestamp, message);
        fflush(log_file);
    } else {
        fprintf(stderr, "[%s] %s\n", timestamp, message);
    }
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    char address[16] = DEFAULT_ADDRESS;
    char log_file_path[256] = DEFAULT_LOG_FILE;
    int port = DEFAULT_PORT;
    int daemon_mode = 0;
    int opt;

    // Проверка и установка переменных окружения
    char *env_addr = getenv("ADDR");
    char *env_port = getenv("PORT");
    char *env_logfile = getenv("LOGFILE");
    char *env_wait = getenv("WAIT");

    if (env_addr) {
        strncpy(address, env_addr, 15);
        if (!validate_ip(address)) {
            fprintf(stderr, "Ошибка: Неверный IP-адрес в переменной окружения ADDR\n");
            exit(EXIT_FAILURE);
        }
    }
    if (env_port) {
        port = atoi(env_port);
        if (!validate_port(port)) {
            fprintf(stderr, "Ошибка: Неверный номер порта в переменной окружения PORT\n");
            exit(EXIT_FAILURE);
        }
    }
    if (env_logfile) {
        strncpy(log_file_path, env_logfile, 255);
        if (!validate_log_file_path(log_file_path)) {
            fprintf(stderr, "Ошибка: Неверный путь к лог-файлу в переменной окружения LOGFILE\n");
            exit(EXIT_FAILURE);
        }
    }
    if (env_wait) {
        wait_time = atoi(env_wait);
        if (wait_time < 0) {
            fprintf(stderr, "Ошибка: Неверное значение времени ожидания в переменной окружения WAIT\n");
            exit(EXIT_FAILURE);
        }
    }

    // Парсинг аргументов командной строки
    while ((opt = getopt(argc, argv, "a:p:l:w:dvh")) != -1) {
        switch (opt) {
            case 'a':
                if (env_addr) {
                    fprintf(stderr, "Ошибка: Переопределение опции -a переменной окружения ADDR\n");
                    exit(EXIT_FAILURE);
                }
                strncpy(address, optarg, 15);
                if (!validate_ip(address)) {
                    fprintf(stderr, "Ошибка: Неверный IP-адрес\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'p':
                if (env_port) {
                    fprintf(stderr, "Ошибка: Переопределение опции -p переменной окружения PORT\n");
                    exit(EXIT_FAILURE);
                }
                port = atoi(optarg);
                if (!validate_port(port)) {
                    fprintf(stderr, "Ошибка: Неверный номер порта\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'l':
                if (env_logfile) {
                    fprintf(stderr, "Ошибка: Переопределение опции -l переменной окружения LOGFILE\n");
                    exit(EXIT_FAILURE);
                }
                strncpy(log_file_path, optarg, 255);
                if (!validate_log_file_path(log_file_path)) {
                    fprintf(stderr, "Ошибка: Неверный путь к лог-файлу\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'w':
                if (env_wait) {
                    fprintf(stderr, "Ошибка: Переопределение опции -w переменной окружения WAIT\n");
                    exit(EXIT_FAILURE);
                }
                wait_time = atoi(optarg);
                if (wait_time < 0) {
                    fprintf(stderr, "Ошибка: Неверное значение времени ожидания\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'd':
                daemon_mode = 1;
                break;
            case 'h':
                printf("Usage: %s [-a address] [-p port] [-l log_file] [-w wait_time] [-d (as daemon)] [-v (version)]\n", argv[0]);
                return 0;
            case 'v':
                printf("Version: 1.0\n");
                return 0;
            default:
                return 1;
        }
    }

    // Открытие лог-файла
    log_file = fopen(log_file_path, "a");
    if (!log_file) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }

    if (daemon_mode) {
        if (daemon(0, 0) < 0) {
            perror("Failed to run as daemon");
            exit(EXIT_FAILURE);
        }
    }

    // Установка обработчиков сигналов
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);

    // Создание серверного сокета
    server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd < 0) {
        log_message("Failed to create server socket");
        exit(EXIT_FAILURE);
    }

    // Установка опций сокета
    int optval = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    // Привязка серверного сокета
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(address);
    server_addr.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        log_message("Failed to bind server socket");
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    log_message("Server started, waiting for connections...");

    // Главный цикл сервера
    while (running) {
        bytes_read = recvfrom(server_fd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr*)&client_addr, &client_addr_len);
        if (bytes_read < 0) {
            log_message("Failed to receive data");
            perror("recvfrom");
            continue;
        }
        handle_client(&client_addr, buffer, bytes_read);
    }

    log_message("Server is shutting down...");
    // Закрытие лог-файла и серверного сокета
    fclose(log_file);
    close(server_fd);

    return 0;
}
