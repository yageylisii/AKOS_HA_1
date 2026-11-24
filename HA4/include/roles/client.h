#ifndef CLIENT_H
#define CLIENT_H

#include <stdbool.h>

#include "components/status.h"
#include "net/protocol.h"

// общие параметры запуска клиентских программ
typedef struct ClientOptions {
    const char *host;        // адрес сервера: IP или имя узла
    unsigned short port;     // порт сервера
    int index;               // желаемый номер станции или кабины, -1 — любой
    const char *log_path;    // копия вывода клиента в файле
    int wait_seconds;        // сколько ждать запуска сервера
    bool show_board;         // наблюдатель: печатать сводную таблицу
} ClientOptions;

/*
разбор командной строки клиента: АДРЕС ПОРТ [НОМЕР] [--log ФАЙЛ] [--wait С].
Для станции номер задаётся буквой A или B, для кабины — числом от 1
*/
Status client_parse(int argc, char **argv, int role, ClientOptions *options);

/*
подключение к серверу и регистрация роли. Если сервер ещё не запущен,
повторяет попытку раз в секунду в течение wait_seconds
*/
Status client_connect(const ClientOptions *options, int role, int *fd, WelcomeBody *welcome);

/*
цикл участника модели: получить разрешение такта с моделью, выполнить
свой шаг, вывести свои события, вернуть модель серверу. Завершается по
сообщению сервера о конце работы, по закрытию соединения или по Ctrl+C
*/
Status participant_run(int argc, char **argv, int role);

// клиент-наблюдатель: журнал всей системы и сводная таблица состояния
Status observer_run(int argc, char **argv);

#endif
