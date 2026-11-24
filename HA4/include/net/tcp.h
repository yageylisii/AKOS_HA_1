#ifndef TCP_H
#define TCP_H

#include <stdbool.h>
#include <stddef.h>

#include "components/status.h"

/*
обёртки над сокетами TCP. Устроены по :
CreateTCPServerSocket, AcceptTCPConnection и эхо-клиент, но вместо
DieWithError возвращают Status, чтобы сервер не падал из-за одного клиента
*/

enum { MAX_PENDING = 16 }; // глубина очереди listen()

// разбор номера порта из командной строки: 1..65535
Status tcp_parse_port(const char *text, unsigned short *port);

/*
socket + setsockopt(SO_REUSEADDR) + bind + listen. host == NULL означает
INADDR_ANY: сервер принимает соединения со всех сетевых интерфейсов
*/
Status tcp_create_server(const char *host, unsigned short port, int *listen_fd);

// accept с выводом адреса клиента вида 192.168.1.5:50432
Status tcp_accept(int listen_fd, int *client_fd, char *peer, size_t peer_capacity);

// socket + connect; host — IP-адрес или имя узла, разрешаемое getaddrinfo
Status tcp_connect(const char *host, unsigned short port, int *fd);

// отправка всех байт: send может передать только часть буфера
Status tcp_send_all(int fd, const void *data, size_t length);

/*
приём ровно length байт: TCP — поток, одно сообщение может прийти
несколькими частями. closed = true, если соединение закрыто на границе
*/
Status tcp_recv_all(int fd, void *data, size_t length, bool *closed);

// ограничение времени отправки: медленный клиент не должен остановить сервер
void tcp_set_send_timeout(int fd, int seconds);

#endif
