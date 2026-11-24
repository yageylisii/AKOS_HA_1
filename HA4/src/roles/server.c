#define _POSIX_C_SOURCE 200809L

#include "roles/server.h"
#include "asserts.h"
#include "components/model.h"
#include "net/protocol.h"
#include "net/tcp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/*
сервер однопоточный и построен на select() одновременно ждёт новых подключений, команд с клавиатуры
и сообщений от всех клиентов. Такт модели — конечный автомат, который
продвигается по мере прихода ответов участников, поэтому сервер никогда
не блокируется на одном клиенте и в любой момент принимает наблюдателей
*/

enum {
    MAX_OBSERVERS = 32,
    PARTICIPANT_SLOTS = 1 + STATION_COUNT + MAX_CABINS,
    // участники, наблюдатели и запас под ещё не представившиеся соединения
    MAX_CLIENTS = PARTICIPANT_SLOTS + MAX_OBSERVERS + 8,
    REPLY_TIMEOUT_SECONDS = 10,  // участник обязан ответить на такт за это время
    HELLO_TIMEOUT_SECONDS = 10,  // новое соединение обязано представиться
    SEND_TIMEOUT_SECONDS = 3,    // предел отправки медленному клиенту
    SLOT_OPERATOR = 0,
    SLOT_FIRST_STATION = 1,
    SLOT_FIRST_CABIN = 1 + STATION_COUNT
};

typedef struct Client {
    int fd;              // -1 — запись свободна
    int role;            // тип Role, -1 — клиент ещё не представился
    int index;           // номер станции, кабины или наблюдателя
    char peer[64];       // адрес клиента для вывода
    double connected_at; // момент подключения, для предела на приветствие
    FrameReader reader;  // накопитель частично пришедших кадров
    unsigned char *output; // очередь исходящих кадров для неблокирующего send
    size_t output_start;
    size_t output_end;
    double output_since;
} Client;

typedef struct Server {
    Model model;         // единственная настоящая копия модели
    EventLog group;      // события текущей модельной секунды от всех участников
    Model incoming;      // модель, пришедшая от участника, до проверки
    unsigned char payload[MODEL_WIRE_CAPACITY]; // тело очередного кадра

    int listen_fd;
    int out_fd;          // вывод журнала, обычно STDOUT_FILENO
    int file_fd;         // копия журнала в файле или -1
    bool stdin_open;     // клавиатура ещё не закрыта
    bool role_only;      // без интегрированного журнала в базовом режиме

    Client clients[MAX_CLIENTS];
    int slots[PARTICIPANT_SLOTS]; // номер записи клиента в каждом месте участника или -1
    int slot_count;               // оператор, станции и кабины текущей модели
    int observer_count;
    int observer_seq;             // сквозная нумерация наблюдателей

    bool started;        // все участники хотя бы раз собрались, модель запущена
    bool tick_active;    // такт начат, ждём ответов участников
    int step_slot;       // чей ответ ожидается в текущем такте
    double step_deadline;
    double next_tick_at; // момент начала следующего такта с учётом задержки отображения
    bool paused;         // нет одного из участников, о паузе уже сообщено
    bool stop_requested; // команда остановки с клавиатуры
    bool finished;
} Server;

// модель с журналом занимает около 80 КБ, поэтому сервер статический, а не на стеке
static Server g_server;

static volatile sig_atomic_t g_stop_signal = 0;

static void handle_stop_signal(int number)
{
    (void)number;
    g_stop_signal = 1;
}

// сигнал SIGINT (Ctrl+C) и SIGTERM переводят сервер в штатное завершение с эвакуацией
static void install_server_signals(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_stop_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0; // без SA_RESTART: select() прервётся и флаг будет замечен сразу
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    // запись в закрытый клиентом сокет не должна убивать сервер
    struct sigaction ignore;
    memset(&ignore, 0, sizeof(ignore));
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    sigaction(SIGPIPE, &ignore, NULL);
}

static double now_seconds(void)
{
    struct timespec moment;
    clock_gettime(CLOCK_MONOTONIC, &moment);
    return (double)moment.tv_sec + (double)moment.tv_nsec / 1e9;
}

// служебные сообщения сервера: подключения, паузы. Отличаются от журнала модели префиксом
static void server_say(Server *server, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

static void server_say(Server *server, const char *format, ...)
{
    char text[LOG_EVENT_SIZE];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);

    char line[LOG_EVENT_SIZE + 32];
    int length = snprintf(line, sizeof(line), "[сеть] %s\n", text);
    if (length > 0) {
        size_t size = (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1;
        log_write(server->out_fd, line, size);
        log_write(server->file_fd, line, size);
    }
}

static int slot_of(int role, int index)
{
    switch (role) {
        case ROLE_OPERATOR:
            return SLOT_OPERATOR;
        case ROLE_STATION:
            return SLOT_FIRST_STATION + index;
        case ROLE_CABIN:
            return SLOT_FIRST_CABIN + index;
        default:
            return -1;
    }
}

static void slot_title(int slot, char *buffer, size_t capacity)
{
    if (slot == SLOT_OPERATOR) {
        role_title(ROLE_OPERATOR, 0, buffer, capacity);
    } else if (slot < SLOT_FIRST_CABIN) {
        role_title(ROLE_STATION, slot - SLOT_FIRST_STATION, buffer, capacity);
    } else {
        role_title(ROLE_CABIN, slot - SLOT_FIRST_CABIN, buffer, capacity);
    }
}

static void client_title(const Client *client, char *buffer, size_t capacity)
{
    if (client->role < 0) {
        snprintf(buffer, capacity, "клиент %s", client->peer);
        return;
    }
    role_title(client->role, client->index, buffer, capacity);
}

static bool all_participants_present(const Server *server)
{
    for (int slot = 0; slot < server->slot_count; ++slot) {
        if (server->slots[slot] < 0) {
            return false;
        }
    }
    return true;
}

static int present_in_range(const Server *server, int first, int count)
{
    int present = 0;
    for (int slot = first; slot < first + count; ++slot) {
        if (server->slots[slot] >= 0) {
            ++present;
        }
    }
    return present;
}

static void drop_client(Server *server, int index, const char *reason);

/* Очередь ограничена: отстающего наблюдателя отключаем вместо остановки
   модели. select() отдельно сообщает готовность сокета к записи. */
static Status queue_frame(Server *server, int fd, uint32_t type, const void *data, size_t length)
{
    if (fd < 0) { return ERROR; }
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        Client *client = &server->clients[i];
        if (client->fd != fd) { continue; }
        size_t pending = client->output_end - client->output_start;
        size_t total = FRAME_HEADER_SIZE + length;
        if (length > frame_max_payload() || pending + total > 4 * MODEL_WIRE_CAPACITY) {
            return ERROR;
        }
        if (client->output_start > 0) {
            memmove(client->output, client->output + client->output_start, pending);
        }
        client->output_start = 0;
        client->output_end = pending;
        if (pending == 0) { client->output_since = now_seconds(); }
        uint32_t header[3] = {htonl(PROTOCOL_MAGIC), htonl(type), htonl((uint32_t)length)};
        memcpy(client->output + pending, header, sizeof(header));
        if (length > 0) { memcpy(client->output + pending + sizeof(header), data, length); }
        client->output_end += total;
        return SUCCESS;
    }
    return ERROR;
}

static Status queue_model(Server *server, int fd, uint32_t type)
{
    unsigned char payload[MODEL_WIRE_CAPACITY];
    size_t length = 0;
    TRY(model_to_wire(&server->model, payload, sizeof(payload), &length));
    return queue_frame(server, fd, type, payload, length);
}

static Status flush_client(Client *client)
{
    if (client->output_start == client->output_end) { return SUCCESS; }
    ssize_t sent = send(client->fd, client->output + client->output_start,
                        client->output_end - client->output_start, 0);
    if (sent < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
        return SUCCESS;
    }
    if (sent <= 0) { return ERROR; }
    client->output_start += (size_t)sent;
    // Отсчитываем таймаут от последнего прогресса, а не от начала большой посылки.
    client->output_since = now_seconds();
    if (client->output_start == client->output_end) {
        client->output_start = client->output_end = 0;
    }
    return SUCCESS;
}

static void send_to_observers(Server *server, uint32_t type, const void *data, size_t length)
{
    for (int index = 0; index < MAX_CLIENTS; ++index) {
        Client *client = &server->clients[index];
        if (client->fd >= 0 && client->role == ROLE_OBSERVER) {
            if (queue_frame(server, client->fd, type, data, length) != SUCCESS) {
                drop_client(server, index, "переполнена очередь отправки");
            }
        }
    }
}

static void notify_observers(Server *server, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

static void notify_observers(Server *server, const char *format, ...)
{
    char text[LOG_EVENT_SIZE];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    send_to_observers(server, MSG_NOTICE, text, strlen(text));
}

// снимок модели без журнала для сводной таблицы наблюдателя
static void send_state(Server *server, int fd)
{
    log_init(&server->model.log);
    if (queue_model(server, fd, MSG_STATE) != SUCCESS) {
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (server->clients[i].fd == fd) {
                drop_client(server, i, "переполнена очередь отправки");
                break;
            }
        }
    }
}

static void report_registration_progress(Server *server)
{
    server_say(server, "ожидание участников: оператор %d/1, станции %d/%d, кабины %d/%d",
               present_in_range(server, SLOT_OPERATOR, 1),
               present_in_range(server, SLOT_FIRST_STATION, STATION_COUNT), STATION_COUNT,
               present_in_range(server, SLOT_FIRST_CABIN, server->model.cabin_count),
               server->model.cabin_count);
}

// события, записанные в журнал модели, переносятся в группу текущей секунды
static void absorb_model_log(Server *server)
{
    log_merge(&server->group, &server->model.log);
    log_init(&server->model.log);
}

/*
вывод группы событий: на консоль сервера, в файл журнала и всем наблюдателям
вместе со снимком состояния. Задержка отображения не выполняется сном:
сервер просто откладывает следующий такт и всё это время продолжает
обслуживать подключения
*/
static void publish_group(Server *server)
{
    if (!server->group.has_events) {
        server->next_tick_at = now_seconds();
        return;
    }
    send_to_observers(server, MSG_EVENTS, server->group.buffer, server->group.length);
    for (int index = 0; index < MAX_CLIENTS; ++index) {
        Client *client = &server->clients[index];
        if (client->fd >= 0 && client->role == ROLE_OBSERVER) {
            send_state(server, client->fd);
        }
    }
    log_flush(&server->group, server->role_only ? -1 : server->out_fd,
              server->role_only ? -1 : server->file_fd, 0);
    server->next_tick_at = now_seconds() + server->model.settings.display_delay_ms / 1000.0;
}

static void close_client(Server *server, int index)
{
    Client *client = &server->clients[index];
    if (client->fd >= 0) {
        close(client->fd);
    }
    frame_reader_free(&client->reader);
    free(client->output);
    client->output = NULL;
    client->output_start = client->output_end = 0;
    client->fd = -1;
    client->role = -1;
    client->index = -1;
}

// отключение клиента. Место участника освобождается, модель на сервере не страдает
static void drop_client(Server *server, int index, const char *reason)
{
    Client *client = &server->clients[index];
    char title[64];
    client_title(client, title, sizeof(title));
    server_say(server, "%s отключился: %s", title, reason);

    if (client->role == ROLE_OBSERVER) {
        --server->observer_count;
    } else if (client->role >= 0) {
        int slot = slot_of(client->role, client->index);
        if (slot >= 0 && server->slots[slot] == index) {
            server->slots[slot] = -1;
        }
        if (!server->finished) {
            notify_observers(server, "⚠️  %s отключился (%s)", title, reason);
        }
    }
    close_client(server, index);
}

/*
выдача разрешения такта очередному участнику. Если его места сейчас нет
(клиент отключился), такт замирает на этом участнике до переподключения:
модель не меняется, модельное время стоит
*/
static void dispatch_step(Server *server)
{
    int client_index = server->slots[server->step_slot];
    if (client_index < 0) {
        if (!server->paused) {
            char title[64];
            slot_title(server->step_slot, title, sizeof(title));
            server->paused = true;
            server_say(server, "⏸ моделирование приостановлено: нет связи с участником «%s», "
                       "ожидается переподключение", title);
            notify_observers(server, "⏸ моделирование приостановлено: ждём, когда снова "
                             "подключится %s", title);
        }
        return;
    }

    if (server->paused) {
        server->paused = false;
        server_say(server, "▶ все участники на месте, моделирование продолжается");
        notify_observers(server, "▶ все участники на месте, моделирование продолжается");
    }

    Client *client = &server->clients[client_index];
    log_init(&server->model.log);
    server->step_deadline = now_seconds() + REPLY_TIMEOUT_SECONDS;
    if (queue_model(server, client->fd, MSG_STEP) != SUCCESS) {
        drop_client(server, client_index, "ошибка отправки");
        dispatch_step(server);
    }
}

static void begin_tick(Server *server)
{
    if (!server->started) {
        server->started = true;
        server->model.status = SERVICE_RUNNING;
        server_say(server, "все участники подключены, моделирование начинается");
        notify_observers(server, "▶ все участники подключены, моделирование начинается");
        log_event(&server->model.log, server->model.current_time,
                  "🧢 Начало работы канатной дороги");
        absorb_model_log(server);
    }
    server->tick_active = true;
    server->step_slot = 0;
    dispatch_step(server);
}

// завершение работы: итоги на консоль и в файл, всем клиентам — MSG_SHUTDOWN
static void finish_server(Server *server, const char *reason)
{
    char report[1024];
    if (model_report_text(&server->model, report, sizeof(report)) != SUCCESS) {
        snprintf(report, sizeof(report), "\n");
    }
    if (!server->role_only) {
        log_write(server->out_fd, report, strlen(report));
        log_write(server->file_fd, report, strlen(report));
    }

    char message[1400];
    snprintf(message, sizeof(message), "%s\n%s", reason, server->role_only ? "" : report);
    int notified = 0;
    for (int index = 0; index < MAX_CLIENTS; ++index) {
        Client *client = &server->clients[index];
        if (client->fd < 0) {
            continue;
        }
        if (client->role == ROLE_OBSERVER) {
            send_state(server, client->fd);
        }
        if (queue_frame(server, client->fd, MSG_SHUTDOWN, message, strlen(message)) == SUCCESS) {
            ++notified;
        }
        // Очереди будут дописаны перед закрытием сокетов в drain_clients().
    }
    server_say(server, "сервер завершает работу, уведомлено клиентов: %d", notified);
    server->finished = true;
}

// аварийная остановка по Ctrl+C или по команде с клавиатуры
static void stop_with_evacuation(Server *server, const char *why)
{
    Model *model = &server->model;
    model->interrupted = true;
    if (model->status != SERVICE_FINISHED) {
        log_event(&model->log, model->current_time,
                  "🛑 %s, выполняется аварийная эвакуация", why);
        model_evacuate(model);
    }
    absorb_model_log(server);
    publish_group(server);
    finish_server(server, "🛑 Работа канатной дороги прервана на сервере");
}

// окончание такта: то же, что делал ведущий процесс HA3 после барьера
static void finish_tick(Server *server)
{
    Model *model = &server->model;
    server->tick_active = false;

    if (!model_check_invariants(model)) {
        log_event(&model->log, model->current_time,
                  "🛑 Нарушен инвариант модели: %s", model->invariant_message);
    }

    if (model->invariant_failed) {
        model_evacuate(model);
    } else if (model_is_complete(model)) {
        log_event(&model->log, model->current_time,
                  "💵 Все принятые пассажиры доставлены, кабины на стоянке");
        model->status = SERVICE_FINISHED;
    } else if (model->current_time > model->settings.work_time + MAX_DRAIN_TIME) {
        // защита от бесконечного ожидания при необычном сочетании остановок
        log_event(&model->log, model->current_time,
                  "🛑 Превышено предельное время завершения работы");
        model_evacuate(model);
    }

    absorb_model_log(server);
    publish_group(server);

    if (model->status == SERVICE_FINISHED) {
        finish_server(server, model->invariant_failed
                                  ? "🛑 Работа завершена аварийно: нарушен инвариант"
                                  : "✅ Работа канатной дороги завершена штатно");
        return;
    }
    ++model->current_time;
}

/*
участник вернул модель после своего шага. Время и настройки ведёт только
сервер, поэтому эти поля берутся из серверной копии: участник не может
их изменить даже по ошибке
*/
static void accept_step_result(Server *server, int client_index, size_t length)
{
    if (model_from_wire(&server->incoming, server->payload, length) != SUCCESS) {
        drop_client(server, client_index, "прислал повреждённую модель");
        dispatch_step(server);
        return;
    }
    server->incoming.current_time = server->model.current_time;
    server->incoming.settings = server->model.settings;
    server->incoming.cabin_count = server->model.cabin_count;

    server->model = server->incoming;
    absorb_model_log(server);

    ++server->step_slot;
    if (server->step_slot < server->slot_count) {
        dispatch_step(server);
    } else {
        finish_tick(server);
    }
}

// регистрация: клиент сообщил роль и желаемый номер
static void handle_hello(Server *server, int client_index, size_t length)
{
    Client *client = &server->clients[client_index];
    int role = -1;
    int index = -1;
    if (client->role >= 0) {
        drop_client(server, client_index, "повторное приветствие");
        return;
    }
    if (hello_parse(server->payload, length, &role, &index) != SUCCESS) {
        frame_send_text(client->fd, MSG_REJECT, "несовместимая версия клиента");
        drop_client(server, client_index, "несовместимая версия");
        return;
    }

    if (role == ROLE_OBSERVER) {
        if (server->observer_count >= MAX_OBSERVERS) {
            frame_send_text(client->fd, MSG_REJECT, "превышено число наблюдателей");
            drop_client(server, client_index, "превышено число наблюдателей");
            return;
        }
        client->role = ROLE_OBSERVER;
        client->index = server->observer_seq++;
        ++server->observer_count;

    } else {
        int count = role == ROLE_OPERATOR ? 1
                    : role == ROLE_STATION ? STATION_COUNT
                                           : server->model.cabin_count;
        if (index < 0) {
            // первый свободный номер этой роли
            for (int candidate = 0; candidate < count; ++candidate) {
                int candidate_slot = slot_of(role, candidate);
                if (candidate_slot >= 0 && candidate_slot < server->slot_count &&
                    server->slots[candidate_slot] < 0) {
                    index = candidate;
                    break;
                }
            }
        }
        const char *problem = NULL;
        int slot = slot_of(role, index);
        if (index < 0) {
            problem = "все места этой роли уже заняты";
        } else if (index >= count) {
            problem = "такого номера в модели нет";
        } else if (slot < 0 || slot >= server->slot_count) {
            problem = "неверная роль или номер участника";
        } else if (server->slots[slot] >= 0) {
            problem = "это место уже занято другим клиентом";
        }
        if (problem != NULL) {
            frame_send_text(client->fd, MSG_REJECT, problem);
            drop_client(server, client_index, problem);
            return;
        }
        client->role = role;
        client->index = index;
        server->slots[slot] = client_index;
    }

    WelcomeBody welcome = welcome_make(client->role, client->index, server->model.cabin_count,
                                       server->model.settings.seed);
    if (queue_frame(server, client->fd, MSG_WELCOME, &welcome, sizeof(welcome)) != SUCCESS) {
        drop_client(server, client_index, "ошибка отправки");
        return;
    }

    char title[64];
    client_title(client, title, sizeof(title));
    server_say(server, "подключился %s (%s)", title, client->peer);

    if (client->role == ROLE_OBSERVER) {
        char now[16];
        log_format_time(server->model.current_time, now, sizeof(now));
        char greeting[256];
        snprintf(greeting, sizeof(greeting),
                 "👀 вы подключены к канатной дороге как %s, модельное время %s, режим: %s",
                 title, now, model_status_name(&server->model));
        queue_frame(server, client->fd, MSG_NOTICE, greeting, strlen(greeting));
        send_state(server, client->fd);
        return;
    }

    notify_observers(server, "🔌 подключился %s", title);
    if (!server->started) {
        report_registration_progress(server);
    } else if (server->tick_active && server->step_slot == slot_of(client->role, client->index)) {
        // такт ждал именно этого участника: продолжаем с того же места
        dispatch_step(server);
    }
}

static void handle_frame(Server *server, int client_index, uint32_t type, size_t length)
{
    Client *client = &server->clients[client_index];
    if (client->role < 0 && type != MSG_HELLO) {
        drop_client(server, client_index, "не представился");
        return;
    }
    switch (type) {
        case MSG_HELLO:
            handle_hello(server, client_index, length);
            break;
        case MSG_DONE: {
            int slot = slot_of(client->role, client->index);
            if (!server->tick_active || slot != server->step_slot) {
                drop_client(server, client_index, "ответ на такт, который ему не выдавался");
                return;
            }
            accept_step_result(server, client_index, length);
            break;
        }
        default:
            drop_client(server, client_index, "неожиданное сообщение");
            break;
    }
}

static void handle_readable_client(Server *server, int client_index)
{
    Client *client = &server->clients[client_index];
    bool closed = false;
    if (frame_reader_fill(&client->reader, client->fd, &closed) != SUCCESS) {
        drop_client(server, client_index, "переполнение буфера");
        return;
    }
    if (closed) {
        bool was_step_owner = server->tick_active && client->role >= 0 &&
                              client->role != ROLE_OBSERVER &&
                              slot_of(client->role, client->index) == server->step_slot;
        drop_client(server, client_index, "соединение закрыто");
        if (was_step_owner && !server->finished) {
            dispatch_step(server); // объявит паузу
        }
        return;
    }

    // за один recv могло прийти несколько кадров или часть кадра
    for (;;) {
        if (server->clients[client_index].fd < 0 || server->finished) {
            return;
        }
        uint32_t type = 0;
        size_t length = 0;
        bool has_frame = false;
        if (frame_reader_next(&client->reader, &type, server->payload, sizeof(server->payload),
                              &length, &has_frame) != SUCCESS) {
            drop_client(server, client_index, "нарушен протокол");
            return;
        }
        if (!has_frame) {
            return;
        }
        handle_frame(server, client_index, type, length);
    }
}

static void accept_new_client(Server *server)
{
    int fd = -1;
    char peer[64];
    if (tcp_accept(server->listen_fd, &fd, peer, sizeof(peer)) != SUCCESS) {
        return;
    }
    if (fd >= FD_SETSIZE || fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        close(fd);
        return;
    }
    for (int index = 0; index < MAX_CLIENTS; ++index) {
        Client *client = &server->clients[index];
        if (client->fd >= 0) {
            continue;
        }
        client->output = malloc(4 * MODEL_WIRE_CAPACITY);
        if (client->output == NULL) { break; }
        if (frame_reader_init(&client->reader) != SUCCESS) {
            free(client->output);
            client->output = NULL;
            break;
        }
        client->fd = fd;
        client->role = -1;
        client->index = -1;
        client->connected_at = now_seconds();
        snprintf(client->peer, sizeof(client->peer), "%s", peer);
        return;
    }
    frame_send_text(fd, MSG_REJECT, "сервер перегружен");
    close(fd);
}

// команды оператора сервера с клавиатуры
static void handle_console(Server *server)
{
    char line[128];
    ssize_t length;
    do {
        length = read(STDIN_FILENO, line, sizeof(line) - 1);
    } while (length < 0 && errno == EINTR);

    if (length <= 0) {
        // ввод закрыт (запуск из скрипта): сервер работает дальше без консоли
        server->stdin_open = false;
        return;
    }
    line[length] = '\0';
    char *newline = strchr(line, '\n');
    if (newline != NULL) {
        *newline = '\0';
    }

    if (strcmp(line, "q") == 0 || strcmp(line, "quit") == 0 || strcmp(line, "stop") == 0) {
        server->stop_requested = true;
    } else if (strcmp(line, "s") == 0 || strcmp(line, "status") == 0) {
        if (server->role_only) {
            report_registration_progress(server);
            return;
        }
        char board[4096];
        if (model_board_text(&server->model, board, sizeof(board)) == SUCCESS) {
            log_write(server->out_fd, board, strlen(board));
        }
        server_say(server, "подключено наблюдателей: %d", server->observer_count);
    } else if (line[0] != '\0') {
        server_say(server, "команды: s — состояние, q — остановить с эвакуацией");
    }
}

// сроки: участник не ответил на такт, новый клиент не представился
static void check_deadlines(Server *server)
{
    double now = now_seconds();
    if (server->tick_active) {
        int client_index = server->slots[server->step_slot];
        if (client_index >= 0 && now > server->step_deadline) {
            drop_client(server, client_index, "не ответил на такт вовремя");
            dispatch_step(server);
        }
    }
    for (int index = 0; index < MAX_CLIENTS; ++index) {
        Client *client = &server->clients[index];
        if (client->fd >= 0 && client->output_end > client->output_start &&
            now - client->output_since > SEND_TIMEOUT_SECONDS) {
            bool owner = server->tick_active && client->role >= 0 &&
                         client->role != ROLE_OBSERVER &&
                         slot_of(client->role, client->index) == server->step_slot;
            drop_client(server, index, "клиент не читает данные");
            if (owner) { dispatch_step(server); }
            continue;
        }
        if (client->fd >= 0 && client->role < 0 &&
            now - client->connected_at > HELLO_TIMEOUT_SECONDS) {
            drop_client(server, index, "не представился вовремя");
        }
    }
}

// сколько можно ждать в select(), не пропустив ни такт, ни срок ответа
static void compute_timeout(const Server *server, struct timeval *timeout)
{
    double wait = 1.0;
    double now = now_seconds();
    if (server->tick_active) {
        if (server->slots[server->step_slot] >= 0) {
            wait = server->step_deadline - now;
        }
    } else if (all_participants_present(server)) {
        wait = server->next_tick_at - now;
    }
    if (wait > 1.0) {
        wait = 1.0;
    }
    if (wait < 0.0) {
        wait = 0.0;
    }
    timeout->tv_sec = (time_t)wait;
    timeout->tv_usec = (suseconds_t)((wait - (double)timeout->tv_sec) * 1e6);
}

static void server_loop(Server *server)
{
    while (!server->finished) {
        if (g_stop_signal) {
            stop_with_evacuation(server, "Получен сигнал прерывания");
            break;
        }
        if (server->stop_requested) {
            stop_with_evacuation(server, "Оператор сервера остановил работу");
            break;
        }
        if (!server->tick_active && all_participants_present(server) &&
            now_seconds() >= server->next_tick_at) {
            begin_tick(server);
            continue;
        }
        if (!server->tick_active && server->started && !all_participants_present(server) &&
            !server->paused) {
            // пауза между тактами: следующий такт начнётся после переподключения
            server->paused = true;
            server_say(server, "⏸ моделирование приостановлено до переподключения участника");
            notify_observers(server, "⏸ моделирование приостановлено до переподключения участника");
        }

        fd_set readable, writable;
        FD_ZERO(&readable);
        FD_ZERO(&writable);
        int max_fd = server->listen_fd;
        FD_SET(server->listen_fd, &readable);
        if (server->stdin_open) {
            FD_SET(STDIN_FILENO, &readable);
        }
        for (int index = 0; index < MAX_CLIENTS; ++index) {
            int fd = server->clients[index].fd;
            if (fd >= 0) {
                FD_SET(fd, &readable);
                if (server->clients[index].output_end > server->clients[index].output_start) {
                    FD_SET(fd, &writable);
                }
                if (fd > max_fd) {
                    max_fd = fd;
                }
            }
        }

        struct timeval timeout;
        compute_timeout(server, &timeout);
        int ready = select(max_fd + 1, &readable, &writable, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            stop_with_evacuation(server, "Ошибка ожидания событий сервера");
            break;
        }

        if (ready > 0) {
            if (server->stdin_open && FD_ISSET(STDIN_FILENO, &readable)) {
                handle_console(server);
            }
            if (FD_ISSET(server->listen_fd, &readable)) {
                accept_new_client(server);
            }
            for (int index = 0; index < MAX_CLIENTS && !server->finished; ++index) {
                int fd = server->clients[index].fd;
                if (fd >= 0 && FD_ISSET(fd, &readable)) {
                    handle_readable_client(server, index);
                }
                if (server->clients[index].fd == fd && fd >= 0 && FD_ISSET(fd, &writable)) {
                    if (flush_client(&server->clients[index]) != SUCCESS) {
                        bool owner = server->tick_active && server->clients[index].role >= 0 &&
                            server->clients[index].role != ROLE_OBSERVER &&
                            slot_of(server->clients[index].role, server->clients[index].index) == server->step_slot;
                        drop_client(server, index, "ошибка отправки");
                        if (owner && !server->finished) { dispatch_step(server); }
                    }
                }
            }
        }
        if (!server->finished) {
            check_deadlines(server);
        }
    }
}

/* Ограниченное по времени завершение: отдаём накопленные события и SHUTDOWN
   всем читающим клиентам одновременно; зависший клиент не мешает остальным. */
static void drain_clients(Server *server)
{
    double deadline = now_seconds() + SEND_TIMEOUT_SECONDS;
    while (now_seconds() < deadline) {
        fd_set writable;
        FD_ZERO(&writable);
        int max_fd = -1;
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            Client *client = &server->clients[i];
            if (client->fd >= 0 && client->output_end > client->output_start) {
                FD_SET(client->fd, &writable);
                if (client->fd > max_fd) { max_fd = client->fd; }
            }
        }
        if (max_fd < 0) { break; }
        struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000};
        int ready = select(max_fd + 1, NULL, &writable, NULL, &timeout);
        if (ready < 0 && errno != EINTR) { break; }
        if (ready <= 0) { continue; }
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            Client *client = &server->clients[i];
            if (client->fd >= 0 && FD_ISSET(client->fd, &writable)) {
                if (flush_client(client) != SUCCESS) { close_client(server, i); }
            }
        }
    }
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (server->clients[i].fd >= 0) {
            shutdown(server->clients[i].fd, SHUT_WR);
            close_client(server, i);
        }
    }
}

Status server_run(const Settings *settings, const RunOptions *options)
{
    SOFT_ASSERT(settings != NULL && options != NULL, "Не переданы настройки", ERROR);

    Server *server = &g_server;
    memset(server, 0, sizeof(*server));
    TRY(model_init(&server->model, settings));
    log_init(&server->group);
    server->slot_count = 1 + STATION_COUNT + server->model.cabin_count;
    for (int slot = 0; slot < PARTICIPANT_SLOTS; ++slot) {
        server->slots[slot] = -1;
    }
    for (int index = 0; index < MAX_CLIENTS; ++index) {
        server->clients[index].fd = -1;
        server->clients[index].role = -1;
    }
    server->out_fd = STDOUT_FILENO;
    server->stdin_open = true;
    server->role_only = options->role_only;
    server->file_fd = -1;
    if (options->log_path != NULL) {
        server->file_fd = open(options->log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (server->file_fd < 0) {
            perror(options->log_path);
        }
    }

    install_server_signals();
    if (tcp_create_server(options->host, options->port, &server->listen_fd) != SUCCESS) {
        if (server->file_fd >= 0) {
            close(server->file_fd);
        }
        return ERROR;
    }

    server_say(server, "сервер канатной дороги слушает %s:%u, кабин в модели: %d, seed %u",
               options->host != NULL ? options->host : "0.0.0.0", (unsigned)options->port,
               server->model.cabin_count, settings->seed);
    server_say(server, "клиенты: оператор, станции A и B, кабины 1..%d; наблюдатели — в любой момент",
               server->model.cabin_count);
    server_say(server, "команды с клавиатуры: s — состояние, q — остановить с эвакуацией");
    report_registration_progress(server);

    server_loop(server);
    drain_clients(server);

    close(server->listen_fd);
    if (server->file_fd >= 0) {
        close(server->file_fd);
    }
    return server->model.invariant_failed ? ERROR : SUCCESS;
}
