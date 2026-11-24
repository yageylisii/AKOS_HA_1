#define _POSIX_C_SOURCE 200809L

#include "roles/client.h"
#include "asserts.h"
#include "components/model.h"
#include "net/tcp.h"
#include "roles/roles.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { DEFAULT_WAIT_SECONDS = 10, MAX_WAIT_SECONDS = 600 };

// модель с журналом занимает около 80 КБ, поэтому буферы статические
static Model g_model;
static unsigned char g_payload[MODEL_WIRE_CAPACITY]; // тело последнего принятого кадра

static volatile sig_atomic_t g_interrupted = 0;
static int g_log_fd = -1;

static void handle_interrupt(int number)
{
    (void)number;
    g_interrupted = 1;
}

/*
Ctrl+C у клиента означает отключиться. Без SA_RESTART заблокированный
recv() вернёт EINTR, клиент закроет сокет и выйдет, а сервер увидит
отключение и приостановит модель до переподключения
*/
static void install_client_signals(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_interrupt;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    struct sigaction ignore;
    memset(&ignore, 0, sizeof(ignore));
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    sigaction(SIGPIPE, &ignore, NULL);
}

// вывод клиента дублируется в файл, если он задан
static void client_output(const char *text, size_t length)
{
    log_write(STDOUT_FILENO, text, length);
    log_write(g_log_fd, text, length);
}

static void client_say(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void client_say(const char *format, ...)
{
    char text[2048];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    if (length < 0) {
        return;
    }
    size_t size = (size_t)length < sizeof(text) ? (size_t)length : sizeof(text) - 1;
    client_output(text, size);
}

static void client_usage(const char *program, int role)
{
    const char *index_hint = role == ROLE_STATION   ? " A|B"
                             : role == ROLE_CABIN   ? " [НОМЕР_КАБИНЫ]"
                                                    : "";
    fprintf(stderr,
            "Использование: %s АДРЕС_СЕРВЕРА ПОРТ%s [параметры]\n"
            "  --log ФАЙЛ   сохранить вывод клиента в файл\n"
            "  --wait С     ждать запуска сервера до С секунд (по умолчанию %d)\n"
            "%s",
            program, index_hint, DEFAULT_WAIT_SECONDS,
            role == ROLE_OBSERVER ? "  --no-board   не печатать сводную таблицу состояния\n" : "");
}

static Status parse_index(const char *text, int role, int *index)
{
    if (role == ROLE_STATION) {
        if (strcmp(text, "A") == 0 || strcmp(text, "a") == 0 || strcmp(text, "1") == 0) {
            *index = 0;
            return SUCCESS;
        }
        if (strcmp(text, "B") == 0 || strcmp(text, "b") == 0 || strcmp(text, "2") == 0) {
            *index = 1;
            return SUCCESS;
        }
        fprintf(stderr, "Станция задаётся буквой A или B\n");
        return ERROR;
    }
    if (role == ROLE_CABIN) {
        char *end;
        long parsed = strtol(text, &end, 10);
        SOFT_ASSERT(end != text && *end == '\0' && parsed >= 1 && parsed <= MAX_CABINS,
                    "Номер кабины должен быть числом от 1 до 12", ERROR);
        *index = (int)parsed - 1;
        return SUCCESS;
    }
    fprintf(stderr, "Этому клиенту номер не нужен: %s\n", text);
    return ERROR;
}

Status client_parse(int argc, char **argv, int role, ClientOptions *options)
{
    SOFT_ASSERT(options != NULL, "Не переданы параметры", ERROR);
    *options = (ClientOptions){0};
    options->index = -1;
    options->wait_seconds = DEFAULT_WAIT_SECONDS;
    options->show_board = true;

    int positional = 0;
    for (int index = 1; index < argc; ++index) {
        const char *argument = argv[index];
        if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
            client_usage(argv[0], role);
            return ERROR;
        }
        if (strcmp(argument, "--no-board") == 0 && role == ROLE_OBSERVER) {
            options->show_board = false;
            continue;
        }
        if (strcmp(argument, "--log") == 0 || strcmp(argument, "--wait") == 0) {
            if (index + 1 >= argc) {
                client_usage(argv[0], role);
                return ERROR;
            }
            const char *value = argv[++index];
            if (strcmp(argument, "--log") == 0) {
                options->log_path = value;
            } else {
                char *end;
                long parsed = strtol(value, &end, 10);
                SOFT_ASSERT(end != value && *end == '\0' && parsed >= 0 &&
                            parsed <= MAX_WAIT_SECONDS,
                            "Время ожидания сервера: от 0 до 600 секунд", ERROR);
                options->wait_seconds = (int)parsed;
            }
            continue;
        }
        if (argument[0] == '-' && argument[1] == '-') {
            fprintf(stderr, "Неизвестный параметр: %s\n", argument);
            client_usage(argv[0], role);
            return ERROR;
        }

        // позиционные аргументы: адрес, порт, номер
        if (positional == 0) {
            options->host = argument;
        } else if (positional == 1) {
            TRY(tcp_parse_port(argument, &options->port));
        } else if (positional == 2) {
            TRY(parse_index(argument, role, &options->index));
        } else {
            fprintf(stderr, "Лишний аргумент: %s\n", argument);
            return ERROR;
        }
        ++positional;
    }

    if (positional < 2) {
        client_usage(argv[0], role);
        return ERROR;
    }
    if (role == ROLE_STATION && options->index < 0) {
        fprintf(stderr, "Укажите станцию: A или B\n");
        return ERROR;
    }
    return SUCCESS;
}

static void sleep_one_second(void)
{
    struct timespec pause = {.tv_sec = 1, .tv_nsec = 0};
    nanosleep(&pause, NULL);
}

Status client_connect(const ClientOptions *options, int role, int *fd, WelcomeBody *welcome)
{
    SOFT_ASSERT(options != NULL && fd != NULL && welcome != NULL, "Неверные параметры", ERROR);

    int sock = -1;
    for (int attempt = 0;; ++attempt) {
        if (g_interrupted) {
            return ERROR;
        }
        if (tcp_connect(options->host, options->port, &sock) == SUCCESS) {
            break;
        }
        if (attempt >= options->wait_seconds) {
            client_say("❌ не удалось подключиться к серверу %s:%u\n", options->host,
                       (unsigned)options->port);
            return ERROR;
        }
        if (attempt == 0) {
            client_say("… сервер %s:%u пока недоступен, жду до %d с\n", options->host,
                       (unsigned)options->port, options->wait_seconds);
        }
        sleep_one_second();
    }

    HelloBody hello = hello_make(role, options->index);
    if (frame_send(sock, MSG_HELLO, &hello, sizeof(hello)) != SUCCESS) {
        close(sock);
        return ERROR;
    }

    char reply[512];
    uint32_t type = 0;
    size_t length = 0;
    bool closed = false;
    if (frame_recv(sock, &type, reply, sizeof(reply) - 1, &length, &closed) != SUCCESS) {
        client_say("❌ сервер не ответил на регистрацию\n");
        close(sock);
        return ERROR;
    }
    if (type == MSG_REJECT) {
        reply[length] = '\0';
        client_say("❌ сервер отказал в подключении: %s\n", reply);
        close(sock);
        return ERROR;
    }
    if (type != MSG_WELCOME || welcome_parse(reply, length, welcome) != SUCCESS) {
        client_say("❌ неожиданный ответ сервера\n");
        close(sock);
        return ERROR;
    }
    *fd = sock;
    return SUCCESS;
}

static Status open_client_log(const ClientOptions *options)
{
    if (options->log_path == NULL) {
        return SUCCESS;
    }
    g_log_fd = open(options->log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (g_log_fd < 0) {
        perror(options->log_path);
        return ERROR;
    }
    return SUCCESS;
}

static void close_client_log(void)
{
    if (g_log_fd >= 0) {
        close(g_log_fd);
        g_log_fd = -1;
    }
}

// шаг своей роли над копией модели. Логика та же, что в HA3
static void run_own_step(Model *model, int role, int index)
{
    switch (role) {
        case ROLE_OPERATOR:
            operator_step(model);
            break;
        case ROLE_STATION:
            station_step(model, index);
            break;
        case ROLE_CABIN:
            cabin_step(model, index);
            break;
        default:
            break;
    }
}

/*
общий разбор служебных сообщений сервера. Возвращает true, если клиенту
пора завершаться: сервер сообщил об окончании работы
*/
static bool handle_service_message(uint32_t type, const char *text, size_t length)
{
    if (type == MSG_NOTICE) {
        client_say("[сервер] %.*s\n", (int)length, text);
        return false;
    }
    if (type == MSG_SHUTDOWN) {
        client_say("[сервер] %.*s", (int)length, text);
        client_say("👋 сервер завершил работу, клиент закрывается\n");
        return true;
    }
    return false;
}

Status participant_run(int argc, char **argv, int role)
{
    ClientOptions options;
    TRY(client_parse(argc, argv, role, &options));
    TRY(open_client_log(&options));
    install_client_signals();

    int fd = -1;
    WelcomeBody welcome;
    if (client_connect(&options, role, &fd, &welcome) != SUCCESS) {
        close_client_log();
        return ERROR;
    }

    char title[64];
    role_title(welcome.role, welcome.index, title, sizeof(title));
    client_say("✅ подключено к %s:%u как «%s», кабин в модели %d, seed %u\n",
               options.host, (unsigned)options.port, title, welcome.cabin_count, welcome.seed);
    client_say("   выводятся только события этого участника; Ctrl+C — отключиться\n");

    Status result = SUCCESS;
    long long steps = 0;
    for (;;) {
        uint32_t type = 0;
        size_t length = 0;
        bool closed = false;
        Status received = frame_recv(fd, &type, g_payload, sizeof(g_payload), &length, &closed);
        if (g_interrupted) {
            client_say("🔌 %s отключается по Ctrl+C (тактов выполнено: %lld)\n", title, steps);
            break;
        }
        if (received != SUCCESS) {
            if (closed) {
                client_say("👋 сервер закрыл соединение, клиент завершает работу\n");
            } else {
                client_say("❌ ошибка приёма от сервера\n");
                result = ERROR;
            }
            break;
        }

        if (type != MSG_STEP) {
            if (handle_service_message(type, (const char *)g_payload, length)) {
                break;
            }
            continue;
        }

        // разрешение такта: копия модели проверяется, затем выполняется свой шаг
        if (model_from_wire(&g_model, g_payload, length) != SUCCESS) {
            client_say("❌ сервер прислал повреждённую модель\n");
            result = ERROR;
            break;
        }
        log_init(&g_model.log);
        run_own_step(&g_model, welcome.role, welcome.index);
        ++steps;

        if (frame_send_model(fd, MSG_DONE, &g_model) != SUCCESS) {
            client_say("❌ не удалось вернуть модель серверу\n");
            result = ERROR;
            break;
        }
        // свои события печатаются после отправки, чтобы не задерживать такт
        if (g_model.log.has_events) {
            client_output(g_model.log.buffer, g_model.log.length);
        }
    }

    close(fd);
    close_client_log();
    return result;
}

Status observer_run(int argc, char **argv)
{
    ClientOptions options;
    TRY(client_parse(argc, argv, ROLE_OBSERVER, &options));
    TRY(open_client_log(&options));
    install_client_signals();

    int fd = -1;
    WelcomeBody welcome;
    if (client_connect(&options, ROLE_OBSERVER, &fd, &welcome) != SUCCESS) {
        close_client_log();
        return ERROR;
    }
    client_say("✅ наблюдатель %d подключён к %s:%u; Ctrl+C — отключиться без остановки системы\n",
               welcome.index + 1, options.host, (unsigned)options.port);

    static char board[8192];
    Status result = SUCCESS;
    for (;;) {
        uint32_t type = 0;
        size_t length = 0;
        bool closed = false;
        Status received = frame_recv(fd, &type, g_payload, sizeof(g_payload), &length, &closed);
        if (g_interrupted) {
            client_say("🔌 наблюдатель отключается, канатная дорога продолжает работу\n");
            break;
        }
        if (received != SUCCESS) {
            if (closed) {
                client_say("👋 сервер закрыл соединение, наблюдатель завершает работу\n");
            } else {
                client_say("❌ ошибка приёма от сервера\n");
                result = ERROR;
            }
            break;
        }

        switch (type) {
            case MSG_EVENTS:
                // группа событий всей системы за одну модельную секунду
                client_output((const char *)g_payload, length);
                break;
            case MSG_STATE:
                if (!options.show_board) {
                    break;
                }
                if (model_from_wire(&g_model, g_payload, length) == SUCCESS &&
                    model_board_text(&g_model, board, sizeof(board)) == SUCCESS) {
                    client_output(board, strlen(board));
                }
                break;
            default:
                if (handle_service_message(type, (const char *)g_payload, length)) {
                    close(fd);
                    close_client_log();
                    return SUCCESS;
                }
                break;
        }
    }

    close(fd);
    close_client_log();
    return result;
}
