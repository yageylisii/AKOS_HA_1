/*
тесты сетевого протокола без настоящего сервера: кадры передаются через
пару связанных сокетов socketpair, модель — через функции сериализации.
Отдельно проверяется, что прогон модели «через сеть» (каждый шаг над
копией, полученной из байтов) даёт тот же результат, что и прямой прогон
*/

#define _POSIX_C_SOURCE 200809L

#include "components/model.h"
#include "components/queue.h"
#include "net/protocol.h"
#include "roles/roles.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, message)                                            \
    do {                                                                \
        ++g_checks;                                                     \
        if (!(cond)) {                                                  \
            ++g_failures;                                               \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, message); \
        }                                                               \
    } while (0)

// модели крупные, поэтому все рабочие копии статические
static Model g_direct;
static Model g_remote;
static Model g_copy;
static EventLog g_group;
static unsigned char g_wire[MODEL_WIRE_CAPACITY];

static Settings test_settings(unsigned int seed)
{
    Settings settings = {0};
    settings.cabines = 3;
    settings.capacity = 4;
    settings.max_passengers = 30;
    settings.work_time = 300;
    settings.time_between_stations = 25;
    settings.breakdown_duration = 15;
    settings.arrival_interval = 30;
    settings.passengers_per_interval = 4;
    settings.boarding_time = 3;
    settings.unloading_time = 2;
    settings.vip_probability = 0.3;
    settings.breakdown_probability = 0.2;
    settings.wind_limit = 15;
    settings.wind_count = 1;
    settings.wind[0].start = 100;
    settings.wind[0].end = 140;
    settings.wind[0].speed = 20;
    settings.display_delay_ms = 0;
    settings.seed = seed;
    return settings;
}

static void test_hello_and_welcome(void)
{
    HelloBody hello = hello_make(ROLE_CABIN, 4);
    int role = -1;
    int index = -1;
    CHECK(hello_parse(&hello, sizeof(hello), &role, &index) == SUCCESS,
          "корректное приветствие принимается");
    CHECK(role == ROLE_CABIN && index == 4, "роль и номер доходят без искажений");

    HelloBody any = hello_make(ROLE_STATION, -1);
    CHECK(hello_parse(&any, sizeof(any), &role, &index) == SUCCESS && index == -1,
          "номер -1 означает любой свободный");

    HelloBody old = hello;
    old.version = htonl(PROTOCOL_VERSION + 1);
    CHECK(hello_parse(&old, sizeof(old), &role, &index) != SUCCESS,
          "другая версия протокола отклоняется");

    HelloBody foreign = hello;
    foreign.model_size = htonl((uint32_t)model_wire_header_size() + 8);
    CHECK(hello_parse(&foreign, sizeof(foreign), &role, &index) != SUCCESS,
          "клиент с другим размером модели отклоняется");

    HelloBody strange = hello_make(ROLE_COUNT + 3, 0);
    CHECK(hello_parse(&strange, sizeof(strange), &role, &index) != SUCCESS,
          "неизвестная роль отклоняется");
    CHECK(hello_parse(&hello, sizeof(hello) - 1, &role, &index) != SUCCESS,
          "укороченное приветствие отклоняется");

    WelcomeBody welcome = welcome_make(ROLE_OBSERVER, 2, 5, 777u);
    WelcomeBody parsed;
    CHECK(welcome_parse(&welcome, sizeof(welcome), &parsed) == SUCCESS &&
          parsed.role == ROLE_OBSERVER && parsed.index == 2 &&
          parsed.cabin_count == 5 && parsed.seed == 777u,
          "ответ сервера разбирается обратно");
}

static void test_model_wire_roundtrip(void)
{
    Settings settings = test_settings(11);
    CHECK(model_init(&g_direct, &settings) == SUCCESS, "инициализация модели");
    log_event(&g_direct.log, 0, "проверочное событие %d", 1);

    size_t size = model_wire_size(&g_direct);
    CHECK(model_wire_header_size() == 14772, "фиксированный размер схемы протокола 2");
    CHECK(size == model_wire_header_size() + g_direct.log.length,
          "передаётся только занятая часть журнала");
    CHECK(size < sizeof(Model) / 2, "сообщение заметно меньше полной структуры");

    model_to_wire(&g_direct, g_wire, sizeof(g_wire), &size);
    CHECK(g_wire[0] == 0 && g_wire[1] == 0 && g_wire[2] == 0 && g_wire[3] == 3 &&
          g_wire[4] == 0 && g_wire[5] == 0 && g_wire[6] == 0 && g_wire[7] == 4,
          "числа схемы кодируются big-endian, независимо от ABI");
    CHECK(model_from_wire(&g_copy, g_wire, size) == SUCCESS, "модель восстанавливается");
    CHECK(memcmp(&g_copy, &g_direct, sizeof(Model)) == 0, "восстановленная модель совпадает побайтно");
    CHECK(strstr(g_copy.log.buffer, "проверочное событие 1") != NULL,
          "журнал доходит вместе с моделью");

    CHECK(model_from_wire(&g_copy, g_wire, model_wire_header_size() - 1) != SUCCESS,
          "слишком короткая модель отклоняется");
    CHECK(model_from_wire(&g_copy, g_wire, size - 1) != SUCCESS,
          "несовпадение длины журнала обнаруживается");

    // повреждения, которые без проверки привели бы к выходу за границы массивов
    Model *broken = &g_copy;
    *broken = g_direct;
    broken->stations[0].vip.head = QUEUE_CAPACITY + 5;
    model_to_wire(broken, g_wire, sizeof(g_wire), &size);
    CHECK(model_from_wire(&g_remote, g_wire, size) != SUCCESS, "голова очереди вне буфера");

    *broken = g_direct;
    broken->cabin_count = MAX_CABINS + 1;
    model_to_wire(broken, g_wire, sizeof(g_wire), &size);
    CHECK(model_from_wire(&g_remote, g_wire, size) != SUCCESS, "число кабин вне диапазона");

    *broken = g_direct;
    broken->cabins[0].passengers = 1;
    broken->cabins[0].passenger_ids[0] = 150;
    model_to_wire(broken, g_wire, sizeof(g_wire), &size);
    CHECK(model_from_wire(&g_remote, g_wire, size) != SUCCESS, "несуществующий пассажир в кабине");

    *broken = g_direct;
    queue_push(&broken->stations[1].cabins, 9);
    model_to_wire(broken, g_wire, sizeof(g_wire), &size);
    CHECK(model_from_wire(&g_remote, g_wire, size) != SUCCESS, "несуществующая кабина на станции");
}

static void test_frames_over_socket(void)
{
    int pair[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "socketpair");

    const char *text = "привет от сервера";
    CHECK(frame_send_text(pair[0], MSG_NOTICE, text) == SUCCESS, "отправка текстового кадра");
    char buffer[256];
    uint32_t type = 0;
    size_t length = 0;
    bool closed = false;
    CHECK(frame_recv(pair[1], &type, buffer, sizeof(buffer), &length, &closed) == SUCCESS &&
          type == MSG_NOTICE && length == strlen(text) && memcmp(buffer, text, length) == 0,
          "кадр принимается целиком");

    // два кадра одной записью, затем кадр по одному байту: накопитель обязан
    // правильно разрезать поток независимо от того, как TCP его поделил
    FrameReader reader;
    CHECK(frame_reader_init(&reader) == SUCCESS, "создание накопителя");
    frame_send_text(pair[0], MSG_EVENTS, "первый");
    frame_send_text(pair[0], MSG_EVENTS, "второй");
    CHECK(frame_reader_fill(&reader, pair[1], &closed) == SUCCESS && !closed, "чтение потока");
    bool has_frame = false;
    frame_reader_next(&reader, &type, buffer, sizeof(buffer), &length, &has_frame);
    CHECK(has_frame && length == strlen("первый") && memcmp(buffer, "первый", length) == 0,
          "первый кадр из склеенного потока");
    frame_reader_next(&reader, &type, buffer, sizeof(buffer), &length, &has_frame);
    CHECK(has_frame && length == strlen("второй"), "второй кадр из склеенного потока");
    frame_reader_next(&reader, &type, buffer, sizeof(buffer), &length, &has_frame);
    CHECK(!has_frame, "лишних кадров нет");

    int sender[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sender);
    frame_send_text(sender[0], MSG_SHUTDOWN, "конец");
    unsigned char whole[64];
    ssize_t total = recv(sender[1], whole, sizeof(whole), 0);
    for (ssize_t position = 0; position < total; ++position) {
        send(pair[0], whole + position, 1, 0);
        frame_reader_fill(&reader, pair[1], &closed);
        frame_reader_next(&reader, &type, buffer, sizeof(buffer), &length, &has_frame);
        if (position + 1 < total) {
            CHECK(!has_frame, "неполный кадр не выдаётся");
        }
    }
    CHECK(has_frame && type == MSG_SHUTDOWN && length == strlen("конец"),
          "кадр, пришедший по одному байту, собран");

    // чужие данные в соединении
    uint32_t junk[3] = {htonl(0xDEADBEEF), htonl(MSG_STEP), htonl(4)};
    send(pair[0], junk, sizeof(junk), 0);
    frame_reader_fill(&reader, pair[1], &closed);
    CHECK(frame_reader_next(&reader, &type, buffer, sizeof(buffer), &length, &has_frame) != SUCCESS,
          "кадр с чужой сигнатурой отклоняется");

    close(pair[0]);
    reader.used = 0;
    frame_reader_fill(&reader, pair[1], &closed);
    CHECK(closed, "закрытие соединения замечается");

    frame_reader_free(&reader);
    close(pair[1]);
    close(sender[0]);
    close(sender[1]);
}

static void test_log_merge(void)
{
    EventLog *source = &g_copy.log;
    log_init(source);
    log_init(&g_group);
    log_event(&g_group, 5, "событие сервера");
    log_event(source, 5, "событие кабины");
    log_event(source, 5, "ещё событие кабины");
    CHECK(log_merge(&g_group, source) == SUCCESS, "слияние журналов");
    CHECK(strstr(g_group.buffer, "событие кабины") != NULL &&
          strstr(g_group.buffer, "ещё событие кабины") != NULL, "события перенесены");

    int headers = 0;
    for (const char *cursor = g_group.buffer; (cursor = strstr(cursor, "[09:00:05]")) != NULL;
         ++cursor) {
        ++headers;
    }
    CHECK(headers == 1, "заголовок группы один, заголовок участника не дублируется");
}

/*
сервер передаёт участнику модель, участник возвращает её изменённой.
Если сериализация хоть что-то теряет, прогон через «сеть» разойдётся с прямым
*/
static void network_step(int role, int index)
{
    log_init(&g_remote.log);
    size_t size = model_wire_size(&g_remote);
    model_to_wire(&g_remote, g_wire, sizeof(g_wire), &size);
    if (model_from_wire(&g_copy, g_wire, size) != SUCCESS) {
        ++g_failures;
        return;
    }
    log_init(&g_copy.log);
    if (role == ROLE_OPERATOR) {
        operator_step(&g_copy);
    } else if (role == ROLE_STATION) {
        station_step(&g_copy, index);
    } else {
        cabin_step(&g_copy, index);
    }
    size = model_wire_size(&g_copy);
    model_to_wire(&g_copy, g_wire, sizeof(g_wire), &size);
    if (model_from_wire(&g_remote, g_wire, size) != SUCCESS) {
        ++g_failures;
        return;
    }
    log_merge(&g_group, &g_remote.log);
}

static void direct_tick(Model *model)
{
    operator_step(model);
    for (int index = 0; index < STATION_COUNT; ++index) {
        station_step(model, index);
    }
    for (int index = 0; index < model->cabin_count; ++index) {
        cabin_step(model, index);
    }
    log_init(&model->log);
}

static void test_network_equivalence(unsigned int seed)
{
    Settings settings = test_settings(seed);
    model_init(&g_direct, &settings);
    model_init(&g_remote, &settings);
    g_direct.status = SERVICE_RUNNING;
    g_remote.status = SERVICE_RUNNING;

    for (int time = 0; time < 2000; ++time) {
        if (model_is_complete(&g_direct) && model_is_complete(&g_remote)) {
            break;
        }
        direct_tick(&g_direct);

        log_init(&g_group);
        network_step(ROLE_OPERATOR, 0);
        for (int index = 0; index < STATION_COUNT; ++index) {
            network_step(ROLE_STATION, index);
        }
        for (int index = 0; index < g_remote.cabin_count; ++index) {
            network_step(ROLE_CABIN, index);
        }
        if (!model_check_invariants(&g_remote)) {
            ++g_failures;
            fprintf(stderr, "FAIL инвариант при seed %u: %s\n", seed, g_remote.invariant_message);
            return;
        }
        ++g_direct.current_time;
        ++g_remote.current_time;
    }

    CHECK(model_is_complete(&g_remote), "прогон через сеть завершается");
    CHECK(memcmp(&g_direct.statistics, &g_remote.statistics, sizeof(Statistics)) == 0,
          "статистика прямого и сетевого прогона совпадает");
    CHECK(g_direct.current_time == g_remote.current_time, "время завершения совпадает");
    CHECK(g_remote.statistics.accepted == g_remote.statistics.delivered,
          "все принятые пассажиры доставлены");
}

int main(void)
{
    test_hello_and_welcome();
    test_model_wire_roundtrip();
    test_frames_over_socket();
    test_log_merge();
    for (unsigned int seed = 1; seed <= 20; ++seed) {
        test_network_equivalence(seed);
    }

    if (g_failures > 0) {
        fprintf(stderr, "Protocol tests failed: %d из %d\n", g_failures, g_checks);
        return EXIT_FAILURE;
    }
    printf("Protocol tests passed: проверок %d\n", g_checks);
    return EXIT_SUCCESS;
}
