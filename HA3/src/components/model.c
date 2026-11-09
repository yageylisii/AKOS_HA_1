#define _POSIX_C_SOURCE 200809L

#include "components/model.h"
#include "asserts.h"
#include "components/queue.h"

#include <stdarg.h>
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
    for (int index = 0; index < settings->wind_count; ++index) {
        SOFT_ASSERT(settings->wind[index].start >= 0 &&
                    settings->wind[index].end > settings->wind[index].start &&
                    settings->wind[index].end <= settings->work_time + MAX_DRAIN_TIME,
                    "Неверный интервал ветра", ERROR);
    }
    SOFT_ASSERT(settings->display_delay_ms >= 0 &&
                settings->display_delay_ms <= MAX_DISPLAY_DELAY_MS,
                "Неверная задержка отображения", ERROR);
    return SUCCESS;
}

Status model_init(SharedModel *model, const Settings *settings)
{
    SOFT_ASSERT(model != NULL && settings != NULL, "Не передана модель", ERROR);
    TRY(validate_settings(settings));

    // семафоры инициализируются отдельно и не должны быть затёрты здесь,
    // поэтому обнуляем модель по полям, а не целиком
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
    model->shutdown = false;
    model->interrupted = false;
    model->invariant_failed = false;
    model->invariant_message[0] = '\0';
    model->registered_cabins = 0;
    model->registered_stations = 0;
    model->registered_operators = 0;
    model->expected_workers = settings->cabines + STATION_COUNT;
    model->start_allowed = false;
    model->operator_rng = settings->seed * 104729u + 1u;
    model->operator_pid = 0;
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
        station->pid = 0;
        station->registered = false;
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

bool model_is_stopped(const SharedModel *model)
{
    return model != NULL && (model->wind_stop || model->technical_stop);
}

bool model_movement_allowed(const SharedModel *model)
{
    if (model == NULL) {
        return false;
    }
    if (model->status == SERVICE_FINISHED || model->status == SERVICE_EVACUATING) {
        return false;
    }
    return !model_is_stopped(model);
}

bool model_in_wind_interval(const SharedModel *model, int time)
{
    if (model == NULL) {
        return false;
    }
    for (int index = 0; index < model->settings.wind_count; ++index) {
        const BadWeather *interval = &model->settings.wind[index];
        if (time >= interval->start && time < interval->end) {
            return true;
        }
    }
    return false;
}

int model_waiting_passengers(const SharedModel *model, int station_index)
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
int model_passengers_in_system(const SharedModel *model)
{
    if (model == NULL) {
        return 0;
    }
    const Statistics *statistics = &model->statistics;
    return statistics->accepted - statistics->delivered - statistics->evacuated;
}

// идёт ли сейчас хоть одна операция: движение, посадка или высадка
bool model_has_active_operations(const SharedModel *model)
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

bool model_all_cabins_parked(const SharedModel *model)
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
bool model_is_complete(const SharedModel *model)
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

void model_evacuate(SharedModel *model)
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
static bool fail_invariant(SharedModel *model, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(model->invariant_message, sizeof(model->invariant_message), format, arguments);
    va_end(arguments);
    model->invariant_failed = true;
    return false;
}

bool model_check_invariants(SharedModel *model)
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

Status model_report(const SharedModel *model, int out_fd, int file_fd)
{
    SOFT_ASSERT(model != NULL, "Не передана модель", ERROR);

    const Statistics *statistics = &model->statistics;
    long long average_wait = statistics->delivered > 0
                                 ? statistics->total_wait / statistics->delivered
                                 : 0;

    char report[1024];
    int length = snprintf(report, sizeof(report),
        "\n=== Итоги работы канатной дороги ===\n"
        "Модельное время завершения: %02d:%02d:%02d\n"
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
        (SERVICE_START_SECONDS + model->current_time) / 3600,
        (SERVICE_START_SECONDS + model->current_time) / 60 % 60,
        (SERVICE_START_SECONDS + model->current_time) % 60,
        statistics->accepted, statistics->delivered, statistics->evacuated,
        statistics->rejected, statistics->trips, statistics->breakdowns,
        statistics->wind_stops, average_wait, model->settings.seed,
        model->invariant_failed ? model->invariant_message : "нарушений не обнаружено");

    SOFT_ASSERT(length > 0 && (size_t)length < sizeof(report), "Слишком длинный отчёт", ERROR);

    if (log_write(out_fd, report, (size_t)length) != SUCCESS) {
        return ERROR;
    }
    return log_write(file_fd, report, (size_t)length);
}
