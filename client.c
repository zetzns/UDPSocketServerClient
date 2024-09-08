#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#define DEFAULT_PORT 12345
#define BUFFER_SIZE 1024

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s -a <server_ip> -p <server_port> -m <message> [-v] [-h]\n", prog_name);
    exit(EXIT_FAILURE); 
}

int validate_port(int port) {
    return port > 0 && port <= 65535;
}

int validate_ip(const char *ip) {
    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip, &(sa.sin_addr)) != 0;
}

int main(int argc, char *argv[]) {
    int sock_fd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char *server_ip = NULL;
    int server_port = DEFAULT_PORT;
    char *message = NULL;
    int opt;
    socklen_t addr_len;
    int debug_mode = 0;  // Для DEBUG

    char *env_addr = getenv("ADDR");
    char *env_port = getenv("PORT");
    char *env_debug = getenv("DEBUG");

    if (env_addr) {
        server_ip = env_addr;
        if (!validate_ip(server_ip)) {
            fprintf(stderr, "Ошибка: Неверный IP-адрес в переменной окружения ADDR\n");
            exit(EXIT_FAILURE);
        }
    }
    if (env_port) {
        server_port = atoi(env_port);
        if (!validate_port(server_port)) {
            fprintf(stderr, "Ошибка: Неверный номер порта в переменной окружения PORT\n");
            exit(EXIT_FAILURE);
        }
    }
    if (env_debug) {
        debug_mode = 1;
    }

    while ((opt = getopt(argc, argv, "a:p:m:vh")) != -1) {
        switch (opt) {
            case 'a':
                if (env_addr) {
                    fprintf(stderr, "Ошибка: Переопределение опции -a переменной окружения ADDR\n");
                    exit(EXIT_FAILURE);
                }
                server_ip = optarg;
                if (!validate_ip(server_ip)) {
                    fprintf(stderr, "Ошибка: Неверный IP-адрес\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'p':
                if (env_port) {
                    fprintf(stderr, "Ошибка: Переопределение опции -p переменной окружения PORT\n");
                    exit(EXIT_FAILURE);
                }
                server_port = atoi(optarg);
                if (!validate_port(server_port)) {
                    fprintf(stderr, "Ошибка: Неверный номер порта\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'm':
                message = optarg;
                break;
            case 'v':
                printf("Version: Единственная и неповтормая by Alexandr Zakurin\n");
                return 0;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
        }
    }

    if (!server_ip || !message) {
        print_usage(argv[0]);
    }
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    addr_len = sizeof(server_addr);
    if (debug_mode) {
        fprintf(stderr, "Debug: Sending message '%s' to %s:%d\n", message, server_ip, server_port);
    }

    if (sendto(sock_fd, message, strlen(message), 0, (struct sockaddr *)&server_addr, addr_len) < 0) {
        perror("sendto");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    ssize_t bytes_read = recvfrom(sock_fd, buffer, BUFFER_SIZE - 1, 0, NULL, NULL);
    if (bytes_read < 0) {
        perror("recvfrom");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    buffer[bytes_read] = '\0';
    printf("Received from server: %s\n", buffer);

    close(sock_fd);
    return 0;
}
