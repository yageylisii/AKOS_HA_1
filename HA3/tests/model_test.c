// Исходные тесты модели канатной дороги.
//
// Процессы и семафоры здесь не нужны: шаги участников вызываются
// последовательно в том же порядке, в каком их открывает ведущий процесс
// (сначала оператор, затем станции и кабины). Это позволяет проверить
// предметную логику и инварианты независимо от межпроцессного обмена.

#define _POSIX_C_SOURCE 200809L

#include "components/model.h"
#include "components/queue.h"
#include "roles/roles.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_failures = 0;
static int g_checks = 0;
static int g_log_fd = -1;

#define CHECK(cond, message)                                            \
    do {                                                                \
        ++g_checks;                                                     \
        if (!(cond)) {                                                  \
            ++g_failures;                                               \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, message); \
        }                                                               \
    } while (0)

static Settings basic_settings(void)
{
    Settings settings = {0};
    settings.cabines = 2;
    settings.capacity = 3;
    settings.max_passengers = 10;
    settings.work_time = 120;
    settings.time_between_stations = 10;
    settings.breakdown_duration = 20;
    settings.arrival_interval = 20;
    settings.passengers_per_interval = 2;
    settings.boarding_time = 3;
    settings.unloading_time = 2;
    settings.vip_probability = 0.5;
    settings.breakdown_probability = 0.0;
    settings.wind_count = 0;
    settings.display_delay_ms = 0;
    settings.seed = 12345;
    return settings;
}

static SharedModel *make_model(const Settings *settings)
{
    SharedModel *model = calloc(1, sizeof(SharedModel));
    if (model == NULL) {
        fprintf(stderr, "Не удалось выделить память под модель\n");
        exit(EXIT_FAILURE);
    }
    if (model_init(model, settings) != SUCCESS) {
        fprintf(stderr, "model_init завершился ошибкой\n");
        exit(EXIT_FAILURE);
    }
    model->status = SERVICE_RUNNING;
    return model;
}

// Один такт модельного времени без семафоров: тот же порядок фаз,
// что и в ведущем процессе.
static void tick(SharedModel *model)
{
    operator_step(model);
    for (int index = 0; index < STATION_COUNT; ++index) {
        station_step(model, index);
    }
    for (int index = 0; index < model->cabin_count; ++index) {
        cabin_step(model, index);
    }
    if (!model_check_invariants(model)) {
        ++g_failures;
        fprintf(stderr, "FAIL инвариант на времени %d: %s\n",
                model->current_time, model->invariant_message);
    }
    log_flush(&model->log, g_log_fd, -1, 0);
    ++model->current_time;
}

static int count_cabins_on_platforms(const SharedModel *model)
{
    int total = 0;
    for (int index = 0; index < STATION_COUNT; ++index) {
        total += model->stations[index].cabins.count;
    }
    return total;
}

// ---------------------------------------------------------------------------

static void test_queue(void)
{
    IntQueue queue;
    queue_init(&queue);
    CHECK(queue_is_empty(&queue), "новая очередь должна быть пустой");

    CHECK(queue_push(&queue, 10) && queue_push(&queue, 20) && queue_push(&queue, 30),
          "добавление в очередь");
    CHECK(queue.count == 3, "длина очереди после трёх добавлений");
    CHECK(queue_at(&queue, 0) == 10 && queue_at(&queue, 2) == 30,
          "порядок прибытия сохраняется");

    int value = 0;
    CHECK(queue_pop(&queue, &value) && value == 10, "первым выходит первый пришедший");
    CHECK(queue_remove(&queue, 30) && queue.count == 1, "удаление по значению");
    CHECK(!queue_contains(&queue, 30), "удалённого элемента в очереди нет");
    CHECK(queue_at(&queue, 0) == 20, "оставшийся элемент на месте");
}

// Проверка настроек целиком: конфигурационный файл может содержать что угодно.
// Сообщения SOFT_ASSERT в выводе теста ожидаемы, это и есть проверяемый отказ.
static void test_settings_validation(void)
{
    SharedModel *model = calloc(1, sizeof(SharedModel));
    if (model == NULL) {
        exit(EXIT_FAILURE);
    }

    Settings good = basic_settings();
    CHECK(model_init(model, &good) == SUCCESS, "корректные настройки принимаются");

    struct BadCase {
        const char *name;
        Settings value;
    } bad[] = {
        {"кабин больше предела", good},
        {"нулевая вместимость", good},
        {"вероятность поломки выше допустимой", good},
        {"доля VIP больше единицы", good},
        {"нулевой интервал прибытия", good},
        {"ветер заканчивается раньше, чем начинается", good},
        {"отрицательная задержка отображения", good}
    };
    bad[0].value.cabines = MAX_CABINS + 1;
    bad[1].value.capacity = 0;
    bad[2].value.breakdown_probability = MAX_BREAKDOWN_PROBABILITY + 0.1;
    bad[3].value.vip_probability = 1.5;
    bad[4].value.arrival_interval = 0;
    bad[5].value.wind_count = 1;
    bad[5].value.wind[0].start = 100;
    bad[5].value.wind[0].end = 50;
    bad[6].value.display_delay_ms = -1;

    for (size_t index = 0; index < sizeof(bad) / sizeof(bad[0]); ++index) {
        CHECK(model_init(model, &bad[index].value) == ERROR, bad[index].name);
    }
    free(model);
}

static void test_initial_placement(void)
{
    Settings settings = basic_settings();
    settings.cabines = 10;
    SharedModel *model = make_model(&settings);

    CHECK(count_cabins_on_platforms(model) == 2 * STATION_CABIN_CAPACITY,
          "на станциях занято не более трёх мест каждая");
    CHECK(model->stations[0].cabins.count <= STATION_CABIN_CAPACITY &&
          model->stations[1].cabins.count <= STATION_CABIN_CAPACITY,
          "лимит в три кабины на станцию");

    int parked = 0;
    for (int index = 0; index < model->cabin_count; ++index) {
        if (model->cabins[index].state == PARKED) {
            ++parked;
        }
    }
    CHECK(parked == 10 - 2 * STATION_CABIN_CAPACITY, "остальные кабины в резерве");
    free(model);
}

static void test_passenger_arrival(void)
{
    Settings settings = basic_settings();
    settings.passengers_per_interval = 4;
    settings.max_passengers = 4;
    settings.arrival_interval = 10;
    SharedModel *model = make_model(&settings);

    station_step(model, 0);
    station_step(model, 1);
    CHECK(model->passengers_created == 4, "первая группа появляется сразу в 09:00");
    CHECK(model->statistics.accepted == 4, "все пассажиры приняты в систему");

    model->current_time = 10;
    station_step(model, 0);
    station_step(model, 1);
    CHECK(model->passengers_created == 4, "предел числа пассажиров соблюдается");

    for (int index = 0; index < model->passengers_created; ++index) {
        const Passenger *passenger = &model->passengers[index];
        CHECK(passenger->destination != passenger->origin,
              "пассажир едет на противоположную станцию");
        CHECK(passenger->state == PASSENGER_WAITING, "пассажир ждёт на станции");
    }
    free(model);
}

static void test_boarding_duration_and_fixation(void)
{
    Settings settings = basic_settings();
    settings.cabines = 1;
    settings.capacity = 2;
    settings.boarding_time = 4;
    settings.arrival_interval = 1000;
    settings.passengers_per_interval = 1;
    settings.max_passengers = 1;
    SharedModel *model = make_model(&settings);

    // Один обычный пассажир на станции A.
    station_step(model, 0);
    model->passengers[0].priority = PRIORITY_REGULAR;
    queue_init(&model->stations[0].vip);
    queue_init(&model->stations[0].regular);
    queue_push(&model->stations[0].regular, 1);
    CHECK(model->stations[0].regular.count == 1, "пассажир в обычной очереди");

    Cabin *cabin = &model->cabins[0];
    CHECK(cabin->state == BOARDING && cabin->doors_open, "кабина на станции с открытыми дверями");

    cabin_step(model, 0); // Закрепление пассажира, начало посадки
    CHECK(cabin->boarding_passenger == 1, "пассажир закреплён в начале посадки");
    CHECK(cabin->operation_time_remaining == 4, "длительность посадки задана точно");

    // Позже прибывший VIP не должен занять место посреди операции.
    model->passengers_created = 2;
    model->next_passenger_id = 3;
    model->passengers[1].id = 2;
    model->passengers[1].priority = PRIORITY_VIP;
    model->passengers[1].state = PASSENGER_WAITING;
    model->passengers[1].origin = 0;
    model->passengers[1].destination = 1;
    model->passengers[1].cabin_index = -1;
    queue_push(&model->stations[0].vip, 2);

    for (int step = 0; step < 3; ++step) {
        cabin_step(model, 0);
        CHECK(cabin->boarding_passenger == 1, "закреплённый пассажир не меняется");
    }
    cabin_step(model, 0); // Четвёртая секунда: пассажир внутри, берём следующего
    CHECK(cabin->passengers == 1 && cabin->passenger_ids[0] == 1,
          "первым сел закреплённый пассажир");
    CHECK(model->passengers[0].state == PASSENGER_RIDING, "пассажир в кабине");
    CHECK(cabin->boarding_passenger == 2,
          "следующая операция начинается без скрытой секунды");
    free(model);
}

static void test_doors_and_operator_permission(void)
{
    Settings settings = basic_settings();
    settings.cabines = 1;
    settings.capacity = 1;
    settings.boarding_time = 1;
    settings.arrival_interval = 1000;
    settings.passengers_per_interval = 1;
    settings.max_passengers = 1;
    SharedModel *model = make_model(&settings);

    station_step(model, 0);
    queue_init(&model->stations[0].vip);
    queue_init(&model->stations[0].regular);
    model->passengers[0].origin = 0;
    model->passengers[0].destination = 1;
    model->passengers[0].state = PASSENGER_WAITING;
    queue_push(&model->stations[0].regular, 1);

    Cabin *cabin = &model->cabins[0];
    cabin_step(model, 0); // Закрепление
    cabin_step(model, 0); // Посадка завершена, мест больше нет
    CHECK(cabin->passengers == 1, "кабина заполнена");
    CHECK(cabin->state == ON_STATION && !cabin->doors_open, "двери закрыты перед отправлением");
    CHECK(cabin->departure_requested && !cabin->departure_allowed,
          "кабина ждёт разрешения оператора");

    cabin_step(model, 0);
    CHECK(cabin->state == ON_STATION, "без разрешения кабина не отправляется");

    operator_step(model);
    CHECK(cabin->departure_allowed, "оператор разрешил отправление");

    cabin_step(model, 0);
    CHECK(cabin->state == IN_WALK && !cabin->doors_open,
          "кабина в пути с закрытыми дверями");
    CHECK(!cabin->on_platform, "место на станции освобождено");
    free(model);
}

static void test_capacity_limit(void)
{
    Settings settings = basic_settings();
    settings.cabines = 1;
    settings.capacity = 2;
    settings.boarding_time = 1;
    settings.arrival_interval = 1000;
    settings.passengers_per_interval = 5;
    settings.max_passengers = 5;
    SharedModel *model = make_model(&settings);

    // Пять пассажиров на станции A, вместимость два.
    model->passengers_created = 5;
    model->next_passenger_id = 6;
    queue_init(&model->stations[0].vip);
    queue_init(&model->stations[0].regular);
    for (int index = 0; index < 5; ++index) {
        Passenger *passenger = &model->passengers[index];
        passenger->id = index + 1;
        passenger->priority = PRIORITY_REGULAR;
        passenger->state = PASSENGER_WAITING;
        passenger->origin = 0;
        passenger->destination = 1;
        passenger->cabin_index = -1;
        queue_push(&model->stations[0].regular, passenger->id);
    }

    Cabin *cabin = &model->cabins[0];
    for (int step = 0; step < 10 && cabin->state == BOARDING; ++step) {
        cabin_step(model, 0);
        CHECK(cabin->passengers <= settings.capacity, "вместимость не превышается");
    }
    CHECK(cabin->passengers == 2, "в кабину сели ровно два пассажира");
    CHECK(model->stations[0].regular.count == 3, "остальные ждут следующую кабину");
    CHECK(model->statistics.rejected == 3, "отказ из-за заполнения учтён");
    free(model);
}

static void test_wind_freezes_timers(void)
{
    Settings settings = basic_settings();
    settings.cabines = 1;
    settings.capacity = 2;
    settings.wind_count = 1;
    settings.wind[0].start = 2;
    settings.wind[0].end = 6;
    settings.arrival_interval = 1000;
    settings.passengers_per_interval = 1;
    settings.max_passengers = 1;
    SharedModel *model = make_model(&settings);

    Cabin *cabin = &model->cabins[0];
    cabin->state = IN_WALK;
    cabin->on_platform = false;
    cabin->doors_open = false;
    cabin->operation_time_remaining = 10;
    queue_init(&model->stations[0].cabins);

    model->current_time = 2;
    operator_step(model);
    CHECK(model->wind_stop, "сильный ветер останавливает дорогу");

    int before = cabin->operation_time_remaining;
    cabin_step(model, 0);
    CHECK(cabin->operation_time_remaining == before, "при остановке таймеры заморожены");

    model->current_time = 6;
    operator_step(model);
    CHECK(!model->wind_stop, "после интервала ветра движение возобновляется");

    cabin_step(model, 0);
    CHECK(cabin->operation_time_remaining == before - 1, "таймер снова идёт");
    free(model);
}

static void test_evacuation(void)
{
    Settings settings = basic_settings();
    settings.cabines = 2;
    settings.passengers_per_interval = 4;
    settings.max_passengers = 4;
    SharedModel *model = make_model(&settings);

    station_step(model, 0);
    station_step(model, 1);
    CHECK(model->passengers_created == 4, "пассажиры появились");

    model_evacuate(model);
    CHECK(model->statistics.evacuated == 4, "все оставшиеся пассажиры эвакуированы");
    CHECK(model->status == SERVICE_FINISHED, "после эвакуации модель завершена");
    CHECK(model_all_cabins_parked(model), "кабины выведены на стоянку");
    CHECK(model_passengers_in_system(model) == 0, "в системе не осталось пассажиров");
    CHECK(model_check_invariants(model), "инварианты сохраняются после эвакуации");
    free(model);
}

// Полный прогон до естественного завершения.
static void run_until_complete(SharedModel *model, int limit, const char *label)
{
    int steps = 0;
    while (steps < limit) {
        tick(model);
        if (model_is_complete(model)) {
            model->status = SERVICE_FINISHED;
            break;
        }
        ++steps;
    }
    if (steps >= limit) {
        ++g_failures;
        fprintf(stderr, "FAIL %s: модель не завершилась за %d шагов\n", label, limit);
    }
}

static void test_full_runs(void)
{
    for (int cabins = 1; cabins <= MAX_CABINS; ++cabins) {
        Settings settings = basic_settings();
        settings.cabines = cabins;
        settings.capacity = 1 + cabins % 5;
        settings.max_passengers = 12;
        settings.work_time = 60;
        settings.arrival_interval = 15;
        settings.passengers_per_interval = 3;
        settings.boarding_time = 1 + cabins % 3;
        settings.unloading_time = 1 + cabins % 2;
        settings.time_between_stations = 5 + cabins;
        settings.breakdown_probability = (cabins % 3 == 0) ? 0.3 : 0.0;
        settings.seed = (unsigned int)(1000 + cabins);

        SharedModel *model = make_model(&settings);
        char label[64];
        snprintf(label, sizeof(label), "прогон с %d кабинами", cabins);
        run_until_complete(model, 20000, label);

        CHECK(model->statistics.delivered + model->statistics.evacuated ==
                  model->statistics.accepted,
              "все принятые пассажиры доставлены или эвакуированы");
        CHECK(!model->invariant_failed, "инварианты не нарушены за весь прогон");
        free(model);
    }
}

// Ветер на весь рабочий день: модель завершается по предельному времени.
static void test_drain_timeout(void)
{
    Settings settings = basic_settings();
    settings.cabines = 1;
    settings.work_time = 30;
    settings.arrival_interval = 10;
    settings.passengers_per_interval = 2;
    settings.max_passengers = 4;
    settings.wind_count = 1;
    settings.wind[0].start = 0;
    settings.wind[0].end = MAX_WORK_TIME;
    SharedModel *model = make_model(&settings);

    for (int step = 0; step < 200; ++step) {
        tick(model);
    }
    CHECK(model->wind_stop, "ветер не прекращается");
    CHECK(model_passengers_in_system(model) > 0, "пассажиры остаются в системе");
    CHECK(!model->invariant_failed, "инварианты сохраняются при длительной остановке");

    model_evacuate(model);
    CHECK(model_passengers_in_system(model) == 0, "эвакуация освобождает систему");
    free(model);
}

int main(void)
{
    g_log_fd = open("build/tests/model.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (g_log_fd == -1) {
        g_log_fd = open("/dev/null", O_WRONLY);
    }

    test_queue();
    test_settings_validation();
    test_initial_placement();
    test_passenger_arrival();
    test_boarding_duration_and_fixation();
    test_doors_and_operator_permission();
    test_capacity_limit();
    test_wind_freezes_timers();
    test_evacuation();
    test_full_runs();
    test_drain_timeout();

    if (g_log_fd >= 0) {
        close(g_log_fd);
    }

    if (g_failures == 0) {
        fprintf(stderr, "Model tests passed: проверок %d\n", g_checks);
        return EXIT_SUCCESS;
    }
    fprintf(stderr, "Model tests failed: %d из %d\n", g_failures, g_checks);
    return EXIT_FAILURE;
}
