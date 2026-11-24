#define _POSIX_C_SOURCE 200809L

#include "net/tcp.h"
#include "asserts.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

// Малые управляющие кадры не должны ждать отложенного TCP-подтверждения.
static void tune_connection(int fd)
{
    int enabled = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
}

Status tcp_parse_port(const char *text, unsigned short *port)
{
    SOFT_ASSERT(text != NULL && port != NULL, "Не передан порт", ERROR);
    char *end;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    SOFT_ASSERT(end != text && *end == '\0' && errno == 0,
                "Порт должен быть числом", ERROR);
    SOFT_ASSERT(parsed >= 1 && parsed <= 65535, "Порт должен быть от 1 до 65535", ERROR);
    *port = (unsigned short)parsed;
    return SUCCESS;
}

Status tcp_create_server(const char *host, unsigned short port, int *listen_fd)
{
    SOFT_ASSERT(listen_fd != NULL, "Не передан дескриптор", ERROR);

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (host == NULL) {
        address.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, host, &address.sin_addr) != 1) {
        fprintf(stderr, "Неверный IPv4-адрес для прослушивания: %s\n", host);
        return ERROR;
    }

    int fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        perror("socket");
        return ERROR;
    }

    // повторный запуск сразу после завершения не должен упираться в TIME_WAIT
    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
    }

    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(fd);
        return ERROR;
    }
    if (listen(fd, MAX_PENDING) < 0) {
        perror("listen");
        close(fd);
        return ERROR;
    }
    *listen_fd = fd;
    return SUCCESS;
}

Status tcp_accept(int listen_fd, int *client_fd, char *peer, size_t peer_capacity)
{
    SOFT_ASSERT(client_fd != NULL, "Не передан дескриптор", ERROR);

    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    int fd;
    do {
        fd = accept(listen_fd, (struct sockaddr *)&address, &length);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        perror("accept");
        return ERROR;
    }

    if (peer != NULL && peer_capacity > 0) {
        char ip[INET_ADDRSTRLEN] = "?";
        inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip));
        snprintf(peer, peer_capacity, "%s:%u", ip, (unsigned)ntohs(address.sin_port));
    }
    tune_connection(fd);
    *client_fd = fd;
    return SUCCESS;
}

Status tcp_connect(const char *host, unsigned short port, int *fd)
{
    SOFT_ASSERT(host != NULL && fd != NULL, "Не передан адрес сервера", ERROR);

    // getaddrinfo принимает и IP-адрес, и имя узла, например localhost
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char service[16];
    snprintf(service, sizeof(service), "%u", (unsigned)port);

    struct addrinfo *list = NULL;
    int code = getaddrinfo(host, service, &hints, &list);
    if (code != 0) {
        fprintf(stderr, "Не удалось найти сервер %s: %s\n", host, gai_strerror(code));
        return ERROR;
    }

    Status result = ERROR;
    for (struct addrinfo *item = list; item != NULL; item = item->ai_next) {
        int sock = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (sock < 0) {
            continue;
        }
        int rc;
        do {
            rc = connect(sock, item->ai_addr, item->ai_addrlen);
        } while (rc < 0 && errno == EINTR);
        if (rc == 0) {
            tune_connection(sock);
            *fd = sock;
            result = SUCCESS;
            break;
        }
        close(sock);
    }
    freeaddrinfo(list);
    return result;
}

Status tcp_send_all(int fd, const void *data, size_t length)
{
    const char *cursor = data;
    while (length > 0) {
        ssize_t sent = send(fd, cursor, length, 0);
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent <= 0) {
            return ERROR;
        }
        cursor += sent;
        length -= (size_t)sent;
    }
    return SUCCESS;
}

Status tcp_recv_all(int fd, void *data, size_t length, bool *closed)
{
    char *cursor = data;
    size_t received_total = 0;
    if (closed != NULL) {
        *closed = false;
    }
    while (received_total < length) {
        ssize_t received = recv(fd, cursor + received_total, length - received_total, 0);
        if (received < 0 && errno == EINTR) {
            // прерывание сигналом возвращаем вызывающему: клиент проверит флаг Ctrl+C
            return ERROR;
        }
        if (received < 0) {
            return ERROR;
        }
        if (received == 0) {
            if (closed != NULL) {
                *closed = true;
            }
            return ERROR;
        }
        received_total += (size_t)received;
    }
    return SUCCESS;
}

void tcp_set_send_timeout(int fd, int seconds)
{
    struct timeval timeout = {.tv_sec = seconds, .tv_usec = 0};
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt(SO_SNDTIMEO)");
    }
}
