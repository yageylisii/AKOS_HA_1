#define _POSIX_C_SOURCE 200809L

#include "components/model.h"
#include "asserts.h"
#include "components/queue.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// генератор псевдослучайных чисел
unsigned int model_next_random(unsigned int *state)
{
    if (state == NULL) {
        return 0;
    }
    *state = *state * 1103515245u + 12345u;
    return (*state >> 16) & 0x7FFFu;
}

double model_random_unit(unsigned int *state)
{
    return (double)model_next_random(state) / 32768.0;
}

// проверка настроек целиком
static Status validate_settings(const Settings *settings)
{
    SOFT_ASSERT(settings->cabines > 0 && settings->cabines <= MAX_CABINS,
                "Неверное число кабин", ERROR);
    SOFT_ASSERT(settings->capacity > 0 && settings->capacity <= MAX_CABIN_CAPACITY,
                "Неверная вместимость кабины", ERROR);
    SOFT_ASSERT(settings->max_passengers >= 0 && settings->max_passengers <= MAX_PASSENGERS,
                "Неверный предел числа пассажиров", ERROR);
    SOFT_ASSERT(settings->work_time > 0 && settings->work_time <= MAX_WORK_TIME,
                "Неверная длительность работы", ERROR);
    SOFT_ASSERT(settings->time_between_stations > 0 &&
                settings->time_between_stations <= MAX_TRAVEL_TIME,
                "Неверное время пути", ERROR);
    SOFT_ASSERT(settings->breakdown_duration > 0 &&
                settings->breakdown_duration <= MAX_BREAKDOWN_DURATION,
                "Неверная длительность остановки", ERROR);
    SOFT_ASSERT(settings->arrival_interval > 0 &&
                settings->arrival_interval <= MAX_ARRIVAL_INTERVAL,
                "Неверный интервал прибытия", ERROR);
    SOFT_ASSERT(settings->passengers_per_interval > 0 &&
                settings->passengers_per_interval <= MAX_PASSENGERS_PER_INTERVAL,
                "Неверное число пассажиров за интервал", ERROR);
    SOFT_ASSERT(settings->boarding_time > 0 && settings->boarding_time <= MAX_BOARDING_TIME,
                "Неверное время посадки", ERROR);
    SOFT_ASSERT(settings->unloading_time > 0 && settings->unloading_time <= MAX_UNLOADING_TIME,
                "Неверное время высадки", ERROR);
    SOFT_ASSERT(settings->vip_probability >= 0.0 && settings->vip_probability <= 1.0,
                "Неверная доля VIP", ERROR);
    SOFT_ASSERT(settings->breakdown_probability >= 0.0 &&
                settings->breakdown_probability <= MAX_BREAKDOWN_PROBABILITY,
                "Неверная вероятность поломки", ERROR);
    SOFT_ASSERT(settings->wind_count >= 0 && settings->wind_count <= MAX_WIND_INTERVALS,
                "Неверное число интервалов ветра", ERROR);
    SOFT_ASSERT(settings->wind_limit >= 1 && settings->wind_limit <= 150,
                "Неверный порог скорости ветра", ERROR);
    for (int index = 0; index < settings->wind_count; ++index) {
        SOFT_ASSERT(settings->wind[index].start >= 0 &&
                    settings->wind[index].end > settings->wind[index].start &&
                    settings->wind[index].end <= settings->work_time + MAX_DRAIN_TIME,
                    "Неверный интервал ветра", ERROR);
        SOFT_ASSERT(settings->wind[index].speed >= 0 && settings->wind[index].speed <= 150,
                    "Неверная скорость ветра", ERROR);
    }
    SOFT_ASSERT(settings->display_delay_ms >= 0 &&
                settings->display_delay_ms <= MAX_DISPLAY_DELAY_MS,
                "Неверная задержка отображения", ERROR);
    return SUCCESS;
}

Status model_init(Model *model, const Settings *settings)
{
    SOFT_ASSERT(model != NULL && settings != NULL, "Не передана модель", ERROR);
    TRY(validate_settings(settings));

    // Обнуляем и неиспользуемые слоты: они тоже входят в сетевой снимок.
    memset(model, 0, sizeof(*model));
    model->settings = *settings;
    model->cabin_count = settings->cabines;
    model->current_time = 0;
    model->passengers_created = 0;
    model->next_passenger_id = 1;
    model->status = SERVICE_SETUP;
    model->admission_closed = false;
    model->wind_stop = false;
    model->technical_stop = false;
    model->technical_stop_remaining = 0;
    model->interrupted = false;
    model->invariant_failed = false;
    model->invariant_message[0] = '\0';
    model->operator_rng = settings->seed * 104729u + 1u;
    model->statistics = (Statistics){0};

    log_init(&model->log);

    for (int index = 0; index < STATION_COUNT; ++index) {
        Station *station = &model->stations[index];
        station->id = index + 1;
        queue_init(&station->vip);
        queue_init(&station->regular);
        queue_init(&station->waiting_cabins);
        queue_init(&station->cabins);
        station->rng = settings->seed * 7919u + (unsigned int)index * 104729u + 13u;
    }

    for (int index = 0; index < MAX_CABINS; ++index) {
        Cabin *cabin = &model->cabins[index];
        *cabin = (Cabin){0};
        cabin->id = index + 1;
        cabin->boarding_passenger = -1;
        cabin->state = PARKED;
        cabin->station_index = index % STATION_COUNT;
        cabin->destination_index = cabin->station_index;
        cabin->on_platform = false;
    }

    // как в HA1: при запуске на каждой станции занято не более трёх мест,
    // остальные кабины стоят в резерве вне мест обслуживания
    for (int index = 0; index < model->cabin_count; ++index) {
        Cabin *cabin = &model->cabins[index];
        Station *station = &model->stations[cabin->station_index];
        if (station->cabins.count < STATION_CABIN_CAPACITY) {
            queue_push(&station->cabins, index);
            cabin->on_platform = true;
            cabin->doors_open = true;
            cabin->state = BOARDING;
        }
    }

    for (int index = 0; index < MAX_PASSENGERS; ++index) {
        Passenger *passenger = &model->passengers[index];
        *passenger = (Passenger){0};
        passenger->cabin_index = -1;
        passenger->state = PASSENGER_NONE;
    }

    return SUCCESS;
}

bool model_is_stopped(const Model *model)
{
    return model != NULL && (model->wind_stop || model->technical_stop);
}

bool model_movement_allowed(const Model *model)
{
    if (model == NULL) {
        return false;
    }
    if (model->status == SERVICE_FINISHED || model->status == SERVICE_EVACUATING) {
        return false;
    }
    return !model_is_stopped(model);
}

bool model_in_wind_interval(const Model *model, int time)
{
    if (model == NULL) {
        return false;
    }
    for (int index = 0; index < model->settings.wind_count; ++index) {
        const BadWeather *interval = &model->settings.wind[index];
        if (time >= interval->start && time < interval->end &&
            interval->speed >= model->settings.wind_limit) {
            return true;
        }
    }
    return false;
}

int model_waiting_passengers(const Model *model, int station_index)
{
    if (model == NULL || station_index < 0 || station_index >= STATION_COUNT) {
        return 0;
    }
    const Station *station = &model->stations[station_index];
    return station->vip.count + station->regular.count;
}

// счётчики статистики уже хранят всё нужное, поэтому перебирать
// массив пассажиров не требуется: приём ровно равен сумме доставленных
// и эвакуированных, когда в системе никого не осталось
int model_passengers_in_system(const Model *model)
{
    if (model == NULL) {
        return 0;
    }
    const Statistics *statistics = &model->statistics;
    return statistics->accepted - statistics->delivered - statistics->evacuated;
}

// идёт ли сейчас хоть одна операция: движение, посадка или высадка
bool model_has_active_operations(const Model *model)
{
    if (model == NULL) {
        return false;
    }
    for (int index = 0; index < model->cabin_count; ++index) {
        if (model->cabins[index].operation_time_remaining > 0) {
            return true;
        }
    }
    return false;
}

bool model_all_cabins_parked(const Model *model)
{
    if (model == NULL) {
        return false;
    }
    for (int index = 0; index < model->cabin_count; ++index) {
        if (model->cabins[index].state != PARKED) {
            return false;
        }
    }
    return true;
}

// программа завершается после закрытия входа и доставки всех принятых
// пассажиров, когда все кабины выведены на стоянку
bool model_is_complete(const Model *model)
{
    if (model == NULL) {
        return false;
    }
    if (!model->admission_closed) {
        return false;
    }
    if (model_passengers_in_system(model) > 0) {
        return false;
    }
    return model_all_cabins_parked(model);
}

void model_evacuate(Model *model)
{
    SOFT_ASSERT_VOID(model != NULL, "Не передана модель");

    model->status = SERVICE_EVACUATING;
    log_event(&model->log, model->current_time, "🛑 Аварийная эвакуация канатной дороги");

    for (int index = 0; index < model->passengers_created; ++index) {
        Passenger *passenger = &model->passengers[index];
        if (passenger->state != PASSENGER_WAITING &&
            passenger->state != PASSENGER_ENTERING &&
            passenger->state != PASSENGER_RIDING) {
            continue;
        }
        bool in_cabin = passenger->cabin_index >= 0;
        log_event(&model->log, model->current_time,
                  "🛑 Пассажир %d эвакуирован %s", passenger->id,
                  in_cabin ? "из кабины" : "со станции");
        passenger->state = PASSENGER_EVACUATED;
        passenger->cabin_index = -1;
        passenger->finish_time = model->current_time;
        ++model->statistics.evacuated;
    }

    for (int index = 0; index < model->cabin_count; ++index) {
        Cabin *cabin = &model->cabins[index];
        cabin->passengers = 0;
        cabin->boarding_passenger = -1;
        cabin->doors_open = false;
        cabin->departure_requested = false;
        cabin->departure_allowed = false;
        cabin->operation_time_remaining = 0;
        if (cabin->on_platform) {
            queue_remove(&model->stations[cabin->station_index].cabins, index);
            cabin->on_platform = false;
        }
        queue_remove(&model->stations[cabin->station_index].waiting_cabins, index);
        queue_remove(&model->stations[cabin->destination_index].waiting_cabins, index);
        cabin->state = PARKED;
    }

    for (int index = 0; index < STATION_COUNT; ++index) {
        queue_init(&model->stations[index].vip);
        queue_init(&model->stations[index].regular);
    }

    model->status = SERVICE_FINISHED;
}

// проверка инвариантов задания. Нарушение означает ошибку синхронизации
static bool fail_invariant(Model *model, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(model->invariant_message, sizeof(model->invariant_message), format, arguments);
    va_end(arguments);
    model->invariant_failed = true;
    return false;
}

bool model_check_invariants(Model *model)
{
    if (model == NULL) {
        return false;
    }

    int places[MAX_PASSENGERS];
    for (int index = 0; index < MAX_PASSENGERS; ++index) {
        places[index] = 0;
    }

    for (int index = 0; index < model->cabin_count; ++index) {
        const Cabin *cabin = &model->cabins[index];

        // вместимость кабины не превышается
        if (cabin->passengers < 0 || cabin->passengers > model->settings.capacity) {
            return fail_invariant(model, "кабина %d: пассажиров %d при вместимости %d",
                                  cabin->id, cabin->passengers, model->settings.capacity);
        }
        // кабина не отправляется с открытыми дверями
        if (cabin->doors_open && (cabin->state == IN_WALK || cabin->state == WAITING_FOR_STATION)) {
            return fail_invariant(model, "кабина %d движется с открытыми дверями", cabin->id);
        }
        // посадка и высадка выполняются только на станции
        if ((cabin->state == BOARDING || cabin->state == DROP) && !cabin->on_platform) {
            return fail_invariant(model, "кабина %d выполняет операцию вне станции", cabin->id);
        }
        for (int seat = 0; seat < cabin->passengers; ++seat) {
            int passenger_id = cabin->passenger_ids[seat];
            if (passenger_id < 1 || passenger_id > model->passengers_created) {
                return fail_invariant(model, "кабина %d: неизвестный пассажир %d",
                                      cabin->id, passenger_id);
            }
            ++places[passenger_id - 1];
        }
        if (cabin->boarding_passenger > 0) {
            ++places[cabin->boarding_passenger - 1];
        }
    }

    for (int index = 0; index < STATION_COUNT; ++index) {
        const Station *station = &model->stations[index];
        if (station->cabins.count > STATION_CABIN_CAPACITY) {
            return fail_invariant(model, "станция %s: занято %d мест из %d",
                                  station_name(index), station->cabins.count,
                                  STATION_CABIN_CAPACITY);
        }
        for (int position = 0; position < station->vip.count; ++position) {
            ++places[queue_at(&station->vip, position) - 1];
        }
        for (int position = 0; position < station->regular.count; ++position) {
            ++places[queue_at(&station->regular, position) - 1];
        }
    }

    // пассажир находится либо на станции, либо в одной кабине
    for (int index = 0; index < model->passengers_created; ++index) {
        const Passenger *passenger = &model->passengers[index];
        bool active = passenger->state == PASSENGER_WAITING ||
                      passenger->state == PASSENGER_ENTERING ||
                      passenger->state == PASSENGER_RIDING;
        int expected = active ? 1 : 0;
        if (places[index] != expected) {
            return fail_invariant(model, "пассажир %d учтён %d раз вместо %d",
                                  passenger->id, places[index], expected);
        }
    }

    return true;
}

Status model_report_text(const Model *model, char *buffer, size_t capacity)
{
    SOFT_ASSERT(model != NULL && buffer != NULL && capacity > 0, "Не передана модель", ERROR);

    const Statistics *statistics = &model->statistics;
    long long average_wait = statistics->delivered > 0
                                 ? statistics->total_wait / statistics->delivered
                                 : 0;
    char finish[16];
    log_format_time(model->current_time, finish, sizeof(finish));

    int length = snprintf(buffer, capacity,
        "\n=== Итоги работы канатной дороги ===\n"
        "Модельное время завершения: %s\n"
        "Принято пассажиров:         %d\n"
        "Доставлено пассажиров:      %d\n"
        "Эвакуировано пассажиров:    %d\n"
        "Отказов из-за заполнения:   %d\n"
        "Выполнено рейсов:           %d\n"
        "Технических остановок:      %d\n"
        "Остановок из-за ветра:      %d\n"
        "Среднее время в системе:    %lld с\n"
        "Начальное значение ГСЧ:     %u\n"
        "Инварианты модели:          %s\n",
        finish, statistics->accepted, statistics->delivered, statistics->evacuated,
        statistics->rejected, statistics->trips, statistics->breakdowns,
        statistics->wind_stops, average_wait, model->settings.seed,
        model->invariant_failed ? model->invariant_message : "нарушений не обнаружено");

    SOFT_ASSERT(length > 0 && (size_t)length < capacity, "Слишком длинный отчёт", ERROR);
    return SUCCESS;
}

Status model_report(const Model *model, int out_fd, int file_fd)
{
    char report[1024];
    TRY(model_report_text(model, report, sizeof(report)));
    TRY(log_write(out_fd, report, strlen(report)));
    return log_write(file_fd, report, strlen(report));
}

const char *model_status_name(const Model *model)
{
    if (model == NULL) {
        return "?";
    }
    switch (model->status) {
        case SERVICE_SETUP:
            return "ожидание участников";
        case SERVICE_RUNNING:
            return model->admission_closed ? "вход закрыт, доставка принятых" : "движение";
        case SERVICE_STOPPED:
            return model->wind_stop ? "остановка: сильный ветер" : "остановка: неисправность";
        case SERVICE_EVACUATING:
            return "эвакуация";
        case SERVICE_FINISHED:
            return "работа завершена";
        default:
            return "?";
    }
}

const char *cabin_state_name(int state)
{
    switch (state) {
        case ON_STATION:
            return "ждёт отправления";
        case IN_WALK:
            return "в пути";
        case BOARDING:
            return "посадка";
        case DROP:
            return "высадка";
        case WAITING_FOR_STATION:
            return "ждёт места на станции";
        case PARKED:
            return "на стоянке";
        default:
            return "?";
    }
}

// дописывает форматированный текст в буфер, следя за его границей
static void board_append(char *buffer, size_t capacity, size_t *used, const char *format, ...)
    __attribute__((format(printf, 4, 5)));

static void board_append(char *buffer, size_t capacity, size_t *used, const char *format, ...)
{
    if (*used >= capacity) {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(buffer + *used, capacity - *used, format, arguments);
    va_end(arguments);
    if (written < 0) {
        return;
    }
    *used += (size_t)written;
    if (*used >= capacity) {
        *used = capacity - 1;
    }
}

Status model_board_text(const Model *model, char *buffer, size_t capacity)
{
    SOFT_ASSERT(model != NULL && buffer != NULL && capacity > 0, "Не передана модель", ERROR);

    size_t used = 0;
    buffer[0] = '\0';
    char now[16];
    log_format_time(model->current_time, now, sizeof(now));

    board_append(buffer, capacity, &used, "  ┌─ состояние на %s: %s\n", now,
                 model_status_name(model));
    for (int index = 0; index < STATION_COUNT; ++index) {
        const Station *station = &model->stations[index];
        board_append(buffer, capacity, &used,
                     "  │ станция %s: ждут VIP %d, обычных %d; мест занято %d из %d\n",
                     station_name(index), station->vip.count, station->regular.count,
                     station->cabins.count, STATION_CABIN_CAPACITY);
    }
    for (int index = 0; index < model->cabin_count; ++index) {
        const Cabin *cabin = &model->cabins[index];
        if (cabin->state == IN_WALK) {
            board_append(buffer, capacity, &used,
                         "  │ кабина %d: в пути %s→%s, осталось %d с, пассажиров %d/%d\n",
                         cabin->id, station_name(cabin->station_index),
                         station_name(cabin->destination_index),
                         cabin->operation_time_remaining, cabin->passengers,
                         model->settings.capacity);
        } else {
            board_append(buffer, capacity, &used,
                         "  │ кабина %d: %s у станции %s, пассажиров %d/%d, двери %s\n",
                         cabin->id, cabin_state_name(cabin->state),
                         station_name(cabin->state == WAITING_FOR_STATION
                                          ? cabin->destination_index
                                          : cabin->station_index),
                         cabin->passengers, model->settings.capacity,
                         cabin->doors_open ? "открыты" : "закрыты");
        }
    }
    const Statistics *statistics = &model->statistics;
    board_append(buffer, capacity, &used,
                 "  └─ принято %d, доставлено %d, в системе %d, рейсов %d, отказов %d\n",
                 statistics->accepted, statistics->delivered,
                 model_passengers_in_system(model), statistics->trips, statistics->rejected);
    return SUCCESS;
}

// проверка кольцевой очереди, пришедшей из сети
static bool queue_is_sane(const IntQueue *queue, int min_value, int max_value)
{
    if (queue->head < 0 || queue->head >= QUEUE_CAPACITY ||
        queue->count < 0 || queue->count > QUEUE_CAPACITY) {
        return false;
    }
    for (int position = 0; position < queue->count; ++position) {
        int value = queue_at(queue, position);
        if (value < min_value || value > max_value) {
            return false;
        }
    }
    return true;
}

Status model_from_wire(Model *model, const void *data, size_t length)
{
    SOFT_ASSERT(model != NULL && data != NULL, "Не передана модель", ERROR);
    TRY(model_decode_wire(model, data, length));
    TRY(validate_settings(&model->settings));

    // всё, что потом станет индексом массива, проверяется до использования
    SOFT_ASSERT(model->cabin_count >= 1 && model->cabin_count <= MAX_CABINS,
                "Неверное число кабин в модели", ERROR);
    SOFT_ASSERT(model->settings.capacity >= 1 &&
                model->settings.capacity <= MAX_CABIN_CAPACITY,
                "Неверная вместимость в модели", ERROR);
    SOFT_ASSERT(model->passengers_created >= 0 &&
                model->passengers_created <= MAX_PASSENGERS &&
                model->next_passenger_id == model->passengers_created + 1,
                "Неверный счётчик пассажиров в модели", ERROR);
    SOFT_ASSERT(model->settings.wind_count >= 0 &&
                model->settings.wind_count <= MAX_WIND_INTERVALS,
                "Неверное число интервалов ветра в модели", ERROR);

    SOFT_ASSERT(model->cabin_count == model->settings.cabines && model->current_time >= 0 &&
                model->current_time <= MAX_WORK_TIME + MAX_DRAIN_TIME + 1 &&
                model->status >= SERVICE_SETUP && model->status <= SERVICE_FINISHED,
                "Неверное состояние модели", ERROR);
    for (int index = 0; index < model->passengers_created; ++index) {
        const Passenger *p = &model->passengers[index];
        SOFT_ASSERT(p->id == index + 1 && p->priority >= PRIORITY_VIP &&
                    p->priority <= PRIORITY_REGULAR && p->state >= PASSENGER_WAITING &&
                    p->state <= PASSENGER_EVACUATED && p->origin >= 0 &&
                    p->origin < STATION_COUNT && p->destination == 1 - p->origin &&
                    p->cabin_index >= -1 && p->cabin_index < model->cabin_count,
                    "Неверное состояние пассажира", ERROR);
    }
    for (int index = 0; index < STATION_COUNT; ++index) {
        const Station *station = &model->stations[index];
        SOFT_ASSERT(queue_is_sane(&station->vip, 1, model->passengers_created) &&
                    queue_is_sane(&station->regular, 1, model->passengers_created) &&
                    queue_is_sane(&station->waiting_cabins, 0, model->cabin_count - 1) &&
                    queue_is_sane(&station->cabins, 0, model->cabin_count - 1),
                    "Повреждена очередь станции", ERROR);
    }
    for (int index = 0; index < model->cabin_count; ++index) {
        const Cabin *cabin = &model->cabins[index];
        SOFT_ASSERT(cabin->state >= ON_STATION && cabin->state <= PARKED &&
                    cabin->operation_time_remaining >= 0 &&
                    !(cabin->state == DROP && cabin->passengers == 0),
                    "Неверное состояние кабины", ERROR);
        SOFT_ASSERT(cabin->passengers >= 0 && cabin->passengers <= model->settings.capacity,
                    "Неверное число пассажиров кабины", ERROR);
        SOFT_ASSERT(cabin->station_index >= 0 && cabin->station_index < STATION_COUNT &&
                    cabin->destination_index >= 0 && cabin->destination_index < STATION_COUNT,
                    "Неверная станция кабины", ERROR);
        SOFT_ASSERT(cabin->boarding_passenger == -1 ||
                    (cabin->boarding_passenger >= 1 &&
                     cabin->boarding_passenger <= model->passengers_created),
                    "Неверный пассажир на посадке", ERROR);
        for (int seat = 0; seat < cabin->passengers; ++seat) {
            SOFT_ASSERT(cabin->passenger_ids[seat] >= 1 &&
                        cabin->passenger_ids[seat] <= model->passengers_created,
                        "Неверный пассажир в кабине", ERROR);
        }
    }
    model->invariant_message[sizeof(model->invariant_message) - 1] = '\0';
    return SUCCESS;
}
