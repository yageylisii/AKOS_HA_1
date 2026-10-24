#include "components/service.h"
#include "asserts.h"
#include "threads/runtime.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

// завершает функцию если не SUCCESS
#define TRY(expression) do { if ((expression) != SUCCESS) return ERROR; } while (0)

volatile sig_atomic_t service_interrupted = 0;

// проверяет настройки и допустимые диапазоны
static Status validate_settings(const Settings *settings) {
    SOFT_ASSERT(settings->cabin_count <= MAX_CABINS, "Превышен предел cabin_count", ERROR);
    SOFT_ASSERT(settings->capacity <= MAX_CABIN_CAPACITY, "Превышен предел capacity", ERROR);
    SOFT_ASSERT(settings->max_passengers <= MAX_PASSENGERS, "Превышен предел max_passengers", ERROR);
    SOFT_ASSERT(settings->work_time <= MAX_WORK_TIME, "Превышен предел work_time", ERROR);
    SOFT_ASSERT(settings->travel_time <= MAX_TRAVEL_TIME, "Превышен предел travel_time", ERROR);
    SOFT_ASSERT(settings->breakdown_duration <= MAX_BREAKDOWN_DURATION, "Превышен предел breakdown_duration", ERROR);
    SOFT_ASSERT(settings->arrival_interval <= MAX_ARRIVAL_INTERVAL, "Превышен предел arrival_interval", ERROR);
    SOFT_ASSERT(settings->passengers_per_interval <= MAX_PASSENGERS_PER_INTERVAL, "Превышен предел passengers_per_interval", ERROR);
    SOFT_ASSERT(settings->boarding_time <= MAX_BOARDING_TIME, "Превышен предел boarding_time", ERROR);
    SOFT_ASSERT(settings->unloading_time <= MAX_UNLOADING_TIME, "Превышен предел unloading_time", ERROR);
    SOFT_ASSERT(settings->cabin_count > 0 && settings->capacity > 0, "Число и вместимость кабин должны быть положительными", ERROR);
    SOFT_ASSERT(settings->work_time > 0 && settings->travel_time > 0, "Время должно быть положительным", ERROR);
    SOFT_ASSERT(settings->boarding_time > 0 && settings->unloading_time > 0, "Задайте время посадки и высадки", ERROR);
    SOFT_ASSERT(settings->max_passengers >= 0 && settings->max_passengers < INT_MAX, "Неверный лимит пассажиров", ERROR);
    SOFT_ASSERT(settings->arrival_interval > 0 && settings->passengers_per_interval > 0, "Неверный интервал прибытия", ERROR);
    SOFT_ASSERT(settings->vip_probability >= 0 && settings->vip_probability <= 1, "Неверная доля VIP", ERROR);
    SOFT_ASSERT(settings->breakdown_probability >= 0 && settings->breakdown_probability <= MAX_BREAKDOWN_PROBABILITY && settings->breakdown_duration > 0,
                "Неверные параметры поломки", ERROR);
    SOFT_ASSERT(settings->wind_count >= 0 && settings->wind_count <= MAX_WIND_INTERVALS, "Неверное число интервалов ветра", ERROR);
    for (int index = 0; index < settings->wind_count; ++index) {
        SOFT_ASSERT(settings->wind[index].start >= 0 && settings->wind[index].end > settings->wind[index].start &&
                    settings->wind[index].end <= settings->work_time + MAX_DRAIN_TIME, "Неверный интервал ветра", ERROR);
    }
    SOFT_ASSERT((size_t)settings->cabin_count <= SIZE_MAX / sizeof(Cabin) &&
                (size_t)settings->capacity <= SIZE_MAX / sizeof(Passenger), "Размер массива слишком велик", ERROR);
    return SUCCESS;
}

// завершает потоки и освобождает память модели
void service_destroy(Service *service) {
    if (!service) return;
    if (service->runtime) {
        runtime_destroy(service->runtime);
        free(service->runtime);
        service->runtime = NULL;
    }
    log_destroy(&service->event_log);
    for (int index = 0; index < STATION_COUNT; ++index) {
        queue_clear(&service->stations[index].vip);
        queue_clear(&service->stations[index].regular);
        cabin_queue_clear(&service->stations[index].cabins);
        cabin_queue_clear(&service->stations[index].waiting_cabins);
    }
    if (service->cabins) {
        for (int index = 0; index < service->settings.cabin_count; ++index) {
            free(service->cabins[index].passenger_list);
        }
        free(service->cabins);
        service->cabins = NULL;
    }
}

// создаёт кабины и размещает их на станциях
Status service_init(Service *service) {
    SOFT_ASSERT(service != NULL, "Не передан сервис", ERROR);
    SOFT_ASSERT(service->cabins == NULL, "Сервис уже инициализирован", ERROR);

    TRY(validate_settings(&service->settings));
    log_destroy(&service->event_log);
    // начальные значения сервиса
    service->statistics = (Statistics){0};
    service->current_time = 0;
    service->next_passenger_id = 1;
    service->admission_closed = false;
    service->wind_stop = false;
    service->technical_stop = false;
    service->repair_time_left = 0;
    service->status = SERVICE_RUNNING;
    service->cabins = calloc((size_t)service->settings.cabin_count, sizeof(*service->cabins));

    SOFT_ASSERT(service->cabins != NULL, "Не удалось создать кабины", ERROR);

    // fill stations ids
    for (int index = 0; index < STATION_COUNT; ++index){
        service->stations[index].station_id = index + 1;
    }

    for (int index = 0; index < service->settings.cabin_count; ++index) {
        // генерируем новую кабину
        Cabin *cabin = &service->cabins[index];
        cabin->id = index + 1;
        cabin->departure_station = index % STATION_COUNT + 1;
        cabin->destination_station = 3 - cabin->departure_station;
        cabin->state = PARKED;
        cabin->doors_open = false;
        cabin->passenger_list = calloc((size_t)service->settings.capacity, sizeof(Passenger));

        if (!cabin->passenger_list) {
            service_destroy(service);
            SOFT_ASSERT(0, "Не удалось разместить кабины", ERROR);
        }
        // станция прибытия
        Station *station = &service->stations[cabin->departure_station - 1];

        // достигнут максимальный предел кабинок на станции
        if (station->cabins.count < STATION_CABIN_CAPACITY) {
            if (add_cabin(&station->cabins, cabin->id) != SUCCESS) {
                service_destroy(service);
                return ERROR;
            }
            cabin->state = ON_STATION;
            cabin->doors_open = true;
        }
    }
    return SUCCESS;
}

// возвращает случайное число от нуля включительно до единицы не включительно
static double random_fraction(void) {
    return rand() / ((double)RAND_MAX + 1.0);
}

// обновляет остановки из-за ветра и неисправностей
static Status update_stops(Service *service) {

    bool old_wind = service->wind_stop;
    bool old_technical = service->technical_stop;
    bool was_stopped = old_wind || old_technical;
    service->wind_stop = false;

    for (int index = 0; index < service->settings.wind_count; ++index) {
        BadWeather wind_interval = service->settings.wind[index];
        if (service->current_time >= wind_interval.start && service->current_time < wind_interval.end) service->wind_stop = true;
    }
    if (service->repair_time_left > 0) --service->repair_time_left;
    service->technical_stop = service->repair_time_left > 0;
    bool active_operations = false;
    for (int index = 0; index < service->settings.cabin_count; ++index) {
        if (service->cabins[index].time_left > 0) {
            active_operations = true;
            break;
        }
    }
    // генерируем техническую неисправность рандомно
    if (active_operations && !old_technical && !service->wind_stop && service->current_time > 0 && service->current_time % 60 == 0 &&
        random_fraction() < service->settings.breakdown_probability) {
        service->repair_time_left = service->settings.breakdown_duration;
        service->technical_stop = true;
    }
    if (old_wind != service->wind_stop){
        TRY(log_event(&service->event_log, service->current_time, service->wind_stop ? "🛑 Сильный ветер: остановка" : "✅ Сильный ветер закончился"));
    }
    if (old_technical != service->technical_stop){
        TRY(log_event(&service->event_log, service->current_time, service->technical_stop ? "🛑 Техническая неисправность: остановка" : "✅ Неисправность устранена"));
    }

    bool stopped = service->wind_stop || service->technical_stop;
    service->status = stopped ? SERVICE_STOPPED : SERVICE_RUNNING;
    if (was_stopped && !stopped){
        TRY(log_event(&service->event_log, service->current_time, "✅ Работа возобновлена"));
    }

    return SUCCESS;
}

// создаёт очередную группу пассажиров
static Status arrive_passengers(Service *service) {

    if (service->admission_closed || service->current_time % service->settings.arrival_interval != 0) return SUCCESS;
    // осталось обработать пассажиров
    int count = service->settings.max_passengers - service->statistics.accepted;
    // если их больше чем людей за интервал обрабатываем людей за интревал
    if (count > service->settings.passengers_per_interval) {
        count = service->settings.passengers_per_interval;
    }

    for (int index = 0; index < count; ++index) {
        ActorMessage arrival = {.type = ACTOR_MESSAGE_PASSENGER_CREATED};
        int passenger_id = service->next_passenger_id;
        if (service->runtime != NULL) {
            TRY(runtime_start_passenger(service->runtime, passenger_id));
        }
        TRY(actor_request(service, PASSENGER_ACTOR, passenger_id, &arrival));
    }
    return SUCCESS;
}
// проверяет закрытие входа и завершение всех поездок пассажиров
static bool passengers_finished(const Service *service) {
    return service->admission_closed &&
        service->statistics.accepted == service->statistics.delivered + service->statistics.evacuated;
}

// проверяет что все кабины находятся на стоянке
static bool all_cabins_parked(const Service *service) {
    for (int index = 0; index < service->settings.cabin_count; ++index) {
        if (service->cabins[index].state != PARKED) return false;
    }
    return true;
}
// получает решение кабины и разрешает отправление при работающей дороге
static Status operate_cabin(Service *service, int cabin_id) {
    ActorMessage reply = {.type = CABIN_SERVE};
    TRY(actor_request(service, CABIN_ACTOR, cabin_id, &reply));
    if (reply.depart) {
        SOFT_ASSERT(service->status == SERVICE_RUNNING, "Дорога остановлена", ERROR);
        reply.type = CABIN_DEPART;
        TRY(actor_request(service, CABIN_ACTOR, cabin_id, &reply));
    }
    return SUCCESS;
}

// выполняет один шаг модельного времени
Status service_step(Service *service) {
    SOFT_ASSERT(service != NULL && service->cabins != NULL, "Сервис не инициализирован", ERROR);
    if (service->status == SERVICE_FINISHED) return SUCCESS;
    SOFT_ASSERT(service->status != SERVICE_EVACUATING, "Идёт эвакуация", ERROR);

    if (service->current_time >= service->settings.work_time + MAX_DRAIN_TIME) {
        TRY(log_event(&service->event_log, service->current_time,
            "🛑 Превышено время завершения работы; выполняется эвакуация оставшихся пассажиров"));
        return service_evacuate(service);
    }
    for (int index = 0; index < service->settings.cabin_count; ++index) {
        ActorMessage tick = {.type = CABIN_TICK};
        TRY(actor_request(service, CABIN_ACTOR, index + 1, &tick));
    }
    if (!service->admission_closed && service->current_time >= service->settings.work_time) {
        service->admission_closed = true;
        TRY(log_event(&service->event_log, service->current_time,
            "🛑 Приём новых пассажиров прекращён: время работы вышло"));
    }
    TRY(update_stops(service));
    TRY(arrive_passengers(service));
    if (service->status == SERVICE_RUNNING) {
        for (int index = 0; index < service->settings.cabin_count; ++index) TRY(operate_cabin(service, index + 1));
        for (int index = 0; index < STATION_COUNT; ++index) {
            for (;;) {
                ActorMessage admission = {.type = STATION_ADMIT};
                TRY(actor_request(service, STATION_ACTOR, index + 1, &admission));
                if (admission.cabin_id == 0) break;
                admission.type = CABIN_ADMIT;
                TRY(actor_request(service, CABIN_ACTOR, admission.cabin_id, &admission));
                TRY(operate_cabin(service, admission.cabin_id));
            }
        }
    }
    if (passengers_finished(service) && all_cabins_parked(service)) {
        service->status = SERVICE_FINISHED;
        return SUCCESS;
    }
    SOFT_ASSERT(service->current_time < INT_MAX, "Переполнение модельного времени", ERROR);
    ++service->current_time;
    return SUCCESS;
}
// эвакуирует всех оставшихся пассажиров и завершает модель
Status service_evacuate(Service *service) {
    SOFT_ASSERT(service != NULL && service->cabins != NULL, "Сервис не инициализирован", ERROR);
    service->status = SERVICE_EVACUATING;
    service->admission_closed = true;
    for (int index = 0; index < STATION_COUNT; ++index) {
        ActorMessage request = {.type = STATION_EVACUATE};
        TRY(actor_request(service, STATION_ACTOR, index + 1, &request));
    }
    for (int index = 0; index < service->settings.cabin_count; ++index) {
        ActorMessage request = {.type = CABIN_EVACUATE};
        TRY(actor_request(service, CABIN_ACTOR, index + 1, &request));
    }
    service->status = SERVICE_FINISHED;
    return log_flush(&service->event_log);
}

// запускает потоки и моделирование до завершения или прерывания
Status service_run(Service *service) {
    SOFT_ASSERT(service != NULL && service->cabins != NULL, "Сервис не инициализирован", ERROR);
    if (service->runtime != NULL) return ERROR;
    service_interrupted = 0;
    service->runtime = calloc(1, sizeof(*service->runtime));
    SOFT_ASSERT(service->runtime != NULL, "Не удалось создать runtime потоков", ERROR);
    if (runtime_init(service->runtime, service) != SUCCESS) {
        free(service->runtime);
        service->runtime = NULL;
        return ERROR;
    }
    TRY(log_event(&service->event_log, service->current_time, "🧢 Начало работы канатной дороги 🧢"));

    while (service->status != SERVICE_FINISHED) {
        if (service_interrupted) {
            TRY(log_event(&service->event_log, service->current_time,
                          "🛑 Получен сигнал прерывания; пассажиры эвакуируются"));
            TRY(service_evacuate(service));
            break;
        }
        TRY(service_step(service));
    }

    TRY(log_event(&service->event_log, service->current_time, "💵 Работа завершена. Принято: %d, доставлено: %d, эвакуировано: %d 💵",
                     service->statistics.accepted, service->statistics.delivered, service->statistics.evacuated));
    return log_flush(&service->event_log);
}
