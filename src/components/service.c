#include "components/service.h"
#include "asserts.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

// завершает функцию если не SUCCESS
#define TRY(expression) do { if ((expression) != SUCCESS) return ERROR; } while (0)

// валидируем данные с консоли
static Status validate_settings(const Settings *settings) {
    SOFT_ASSERT(settings->cabines <= MAX_CABINS, "Превышен предел cabines", ERROR);
    SOFT_ASSERT(settings->capacity <= MAX_CABIN_CAPACITY, "Превышен предел capacity", ERROR);
    SOFT_ASSERT(settings->max_passengers <= MAX_PASSENGERS, "Превышен предел max_passengers", ERROR);
    SOFT_ASSERT(settings->work_time <= MAX_WORK_TIME, "Превышен предел work_time", ERROR);
    SOFT_ASSERT(settings->time_between_stations <= MAX_TRAVEL_TIME, "Превышен предел time_between_stations", ERROR);
    SOFT_ASSERT(settings->breakdown_duration <= MAX_BREAKDOWN_DURATION, "Превышен предел breakdown_duration", ERROR);
    SOFT_ASSERT(settings->arrival_interval <= MAX_ARRIVAL_INTERVAL, "Превышен предел arrival_interval", ERROR);
    SOFT_ASSERT(settings->passengers_per_interval <= MAX_PASSENGERS_PER_INTERVAL, "Превышен предел passengers_per_interval", ERROR);
    SOFT_ASSERT(settings->boarding_time <= MAX_BOARDING_TIME, "Превышен предел boarding_time", ERROR);
    SOFT_ASSERT(settings->unloading_time <= MAX_UNLOADING_TIME, "Превышен предел unloading_time", ERROR);
    SOFT_ASSERT(settings->cabines > 0 && settings->capacity > 0, "Число и вместимость кабин должны быть положительными", ERROR);
    SOFT_ASSERT(settings->work_time > 0 && settings->time_between_stations > 0, "Время должно быть положительным", ERROR);
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
    SOFT_ASSERT((size_t)settings->cabines <= SIZE_MAX / sizeof(Cabin) &&
                (size_t)settings->capacity <= SIZE_MAX / sizeof(Passenger), "Размер массива слишком велик", ERROR);
    return SUCCESS;
}

// очищаем сервис очереди логи итд
void service_destroy(Service *service) {
    if (!service) return;
    log_destroy(&service->event_log);
    for (int index = 0; index < STATION_COUNT; ++index) {
        queue_clear(&service->stations[index].vip);
        queue_clear(&service->stations[index].regular);
        cabin_queue_clear(&service->stations[index].cabins);
        cabin_queue_clear(&service->stations[index].waiting_cabins);
    }
    if (service->cabins) {
        for (int index = 0; index < service->settings.cabines; ++index) {
            free(service->cabins[index].passenger_list);
        }
        free(service->cabins);
        service->cabins = NULL;
    }
}

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
    service->technical_stop_remaining = 0;
    service->status = SERVICE_RUNNING;
    service->cabins = calloc((size_t)service->settings.cabines, sizeof(*service->cabins));

    SOFT_ASSERT(service->cabins != NULL, "Не удалось создать кабины", ERROR);

    // fill stations ids
    for (int index = 0; index < STATION_COUNT; ++index){
        service->stations[index].ID_station = index + 1;
    }

    for (int index = 0; index < service->settings.cabines; ++index) {
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

static double random_fraction(void) {
    return rand() / ((double)RAND_MAX + 1.0);
}

// останавливает работу системы из-за технических неисправностей или сильного ветра
static Status update_stops(Service *service) {

    bool old_wind = service->wind_stop;
    bool old_technical = service->technical_stop;
    bool was_stopped = old_wind || old_technical;
    service->wind_stop = false;

    for (int index = 0; index < service->settings.wind_count; ++index) {
        BadWeather wind_interval = service->settings.wind[index];
        if (service->current_time >= wind_interval.start && service->current_time < wind_interval.end) service->wind_stop = true;
    }
    if (service->technical_stop_remaining > 0) --service->technical_stop_remaining;
    service->technical_stop = service->technical_stop_remaining > 0;
    bool active_operations = false;
    for (int index = 0; index < service->settings.cabines; ++index) {
        if (service->cabins[index].operation_time_remaining > 0) {
            active_operations = true;
            break;
        }
    }
    // генерируем техническую неисправность рандомно
    if (active_operations && !old_technical && !service->wind_stop && service->current_time > 0 && service->current_time % 60 == 0 &&
        random_fraction() < service->settings.breakdown_probability) {
        service->technical_stop_remaining = service->settings.breakdown_duration;
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

static Status arrive_passengers(Service *service) {

    if (service->admission_closed || service->current_time % service->settings.arrival_interval != 0) return SUCCESS;
    // осталось обработать пассажиров
    int count = service->settings.max_passengers - service->statistics.accepted;
    // если их больше чем людей за интервал обрабатываем людей за интревал
    if (count > service->settings.passengers_per_interval) {
        count = service->settings.passengers_per_interval;
    }

    for (int index = 0; index < count; ++index) {
        // генерируем пассажира
        Passenger passenger = {.id = service->next_passenger_id, .priority = random_fraction() < service->settings.vip_probability ? 0 : 1};
        // станция появления пассажира A or B 
        int station = rand() % STATION_COUNT;
        // добавляем нового пассажира в очередь
        Queue *passenger_queue = passenger.priority == 0 ? &service->stations[station].vip : &service->stations[station].regular;
        TRY(add_passenger(passenger_queue, passenger));

        // Учитываем принятого пассажира и готовим ID следующего
        ++service->statistics.accepted;
        ++service->next_passenger_id;

        // Добавляем событие прибытия в журнал
        TRY(log_event(&service->event_log, service->current_time, "💫 Пассажир %d прибыл на станцию %s (%s)", passenger.id, station_name(station + 1), passenger.priority == 0 ? "⚜️ VIP" : "обычный"));
    }
    return SUCCESS;
}
// считаем сколько пассажиров ожидает на станции
static int waiting_passengers(const Station *station) {
    return station->vip.count + station->regular.count;
}

// сверяем что все принятые пассажиры лиюбо доставлены либо эвакуированы
static bool passengers_finished(const Service *service) {
    return service->admission_closed &&
        service->statistics.accepted == service->statistics.delivered + service->statistics.evacuated;
}

// все ли кабины отправились на стоянку
static bool all_cabins_parked(const Service *service) {
    for (int index = 0; index < service->settings.cabines; ++index) {
        if (service->cabins[index].state != PARKED) return false;
    }
    return true;
}
// отправляет кабину в рейс
static Status depart(Service *service, Station *station, Cabin *cabin) {
    // проверяем закончилась ли посадка
    SOFT_ASSERT(!cabin->has_boarding_passenger, "Посадка ещё не завершена", ERROR);

    // убираем кабину из очереди станции и закрываем двери
    TRY(remove_cabin(&station->cabins, cabin->id));
    cabin->doors_open = false;

    TRY(log_event(&service->event_log, service->current_time, "🟦 Кабина %d: двери закрыты", cabin->id));
    TRY(log_event(&service->event_log, service->current_time, "📣 Оператор разрешил отправление кабины %d", cabin->id));
    cabin->state = IN_WALK;

    cabin->operation_time_remaining = service->settings.time_between_stations; // время поездки
    TRY(log_event(&service->event_log, service->current_time,
        "🚠 Кабина %d движется со станции %s на станцию %s, пассажиров: %d👥",
        cabin->id, station_name(cabin->departure_station), station_name(cabin->destination_station), cabin->passengers));
    return SUCCESS;
}

// движение и завершение поезки кабин
static Status finish_operations(Service *service) {

    if (service->current_time == 0 || service->status != SERVICE_RUNNING){
        return SUCCESS;
    }

    for (int index = 0; index < service->settings.cabines; ++index) {
        Cabin *cabin = &service->cabins[index];
        // смотрим осталось ли у этой кабины время операции если нет - то идем к коду и в зависмости 
        // от статуса выполняем действия
        if (cabin->operation_time_remaining <= 0){
            continue;
        }
        if (--cabin->operation_time_remaining > 0){
            continue;
        }

        if (cabin->state == IN_WALK) { // добавляем кабину в ожидание на станции
            Station *station = &service->stations[cabin->destination_station - 1];
            TRY(add_cabin(&station->waiting_cabins, cabin->id));
            cabin->state = WAITING_FOR_STATION;
            TRY(log_event(&service->event_log, service->current_time,
                "🚠 Кабина %d закончила путь к станции %s; ожидает допуска, двери закрыты",
                cabin->id, station_name(station->ID_station)));

        } else if (cabin->state == DROP) { // пассажир выгружается из кабины
            Passenger passenger = cabin->passenger_list[--cabin->passengers];
            ++service->statistics.delivered;
            cabin->state = ON_STATION;
            TRY(log_event(&service->event_log, service->current_time,
                "📤 Пассажир %d вышел из кабины %d на станции %s: доставлен",
                passenger.id, cabin->id, station_name(cabin->departure_station)));

        } else if (cabin->state == BOARDING) { // пассажир садится в кабину
            SOFT_ASSERT(cabin->has_boarding_passenger, "Не выбран пассажир для посадки", ERROR);
            Passenger passenger = cabin->boarding_passenger;
            cabin->passenger_list[cabin->passengers++] = passenger;
            cabin->has_boarding_passenger = false;
            TRY(log_event(&service->event_log, service->current_time,
                "📥 Пассажир %d сел в кабину %d", passenger.id, cabin->id));
        }
    }
    return SUCCESS;
}

// обсулживание кабин на станции
static Status serve_cabin(Service *service, Cabin *cabin) {
    // смотрим что кабина на станции
    if (cabin->state != ON_STATION && cabin->state != BOARDING) {
        return SUCCESS;
    }
    if (cabin->operation_time_remaining > 0) { 
        return SUCCESS;
    }

    Station *station = &service->stations[cabin->departure_station - 1];
    // ставим статус высадки когда кабина на станции и имеет пассажиров
    if (cabin->state == ON_STATION && cabin->passengers > 0) {
        cabin->state = DROP;
        cabin->operation_time_remaining = service->settings.unloading_time;
        return SUCCESS;
    }
    // статус посадки, если есть место в кабине И есть ожидающие на станции
    cabin->state = BOARDING;
    if (cabin->passengers < service->settings.capacity && waiting_passengers(station) > 0) {
        // смотрим чью очередь обслужить (в приоритете VIP)
        Queue *queue = station->vip.count ? &station->vip : &station->regular;
        TRY(take_passenger(queue, &cabin->boarding_passenger)); // берем пасссажира 
        cabin->has_boarding_passenger = true;
        cabin->operation_time_remaining = service->settings.boarding_time;
        TRY(log_event(&service->event_log, service->current_time,
            "📥 Пассажир %d начал посадку в кабину %d на станции %s",
            cabin->boarding_passenger.id, cabin->id, station_name(station->ID_station)));
        return SUCCESS;
    }
    // после закрытия входа пустые кабины отправляются лишь за ожидающими людьми
    Station *destination = &service->stations[cabin->destination_station - 1];
    if (cabin->passengers == 0 && waiting_passengers(destination) == 0) {
        // Нет смысла гонять пустую кабину. если перед станцией ждут другие
        // освобождаем место; иначе спокойно ждём следующую группу пассажиров.
        if (!service->admission_closed && station->waiting_cabins.count == 0) return SUCCESS;
        // освобождаем место на стоянке если ожидающих не осталось.
        TRY(remove_cabin(&station->cabins, cabin->id));
        cabin->state = PARKED;
        cabin->doors_open = false;
        TRY(log_event(&service->event_log, service->current_time,
            "🅿️ Кабина %d выведена на стоянку у станции %s", cabin->id, station_name(station->ID_station)));
        return SUCCESS;
    }
    // если кабина уже заполнена а люди остались печатаем лог
    if (cabin->passengers == service->settings.capacity && waiting_passengers(station) > 0)
        TRY(log_event(&service->event_log, service->current_time, "🛑 Кабина %d заполнена: посадка остальных отложена", cabin->id));
    // отправляем загруженную кабину в рейс
    return depart(service, station, cabin);
}

// Принимает кабину и начианет высадку
static Status admit_cabins(Service *service, Station *station) {
    // Прибывшие из рейса получают место раньше кабин из резерва.
    while (station->waiting_cabins.count > 0 && station->cabins.count < STATION_CABIN_CAPACITY) {
        int cabin_id = 0;
        TRY(cabin_queue_peek(&station->waiting_cabins, &cabin_id));
        TRY(add_cabin(&station->cabins, cabin_id));
        TRY(take_cabin(&station->waiting_cabins, &cabin_id));
        Cabin *cabin = &service->cabins[cabin_id - 1];
        cabin->departure_station = station->ID_station;
        cabin->destination_station = 3 - station->ID_station;
        cabin->state = ON_STATION;
        cabin->doors_open = true;
        TRY(log_event(&service->event_log, service->current_time,
            "✅ Кабина %d прибыла на станцию %s, двери открыты", cabin->id, station_name(station->ID_station)));
        // Высадка начинается в этот же момент, независимо от соседних кабин.
        TRY(serve_cabin(service, cabin));
    }
    // После каждой начатой посадки очередь меняется: проверяем именно эту станцию.
    for (int index = 0; index < service->settings.cabines &&
         station->cabins.count < STATION_CABIN_CAPACITY && waiting_passengers(station) > 0; ++index) {
        Cabin *cabin = &service->cabins[index];
        if (cabin->state != PARKED || cabin->departure_station != station->ID_station) continue;
        // Резерв вызывается только для оставшихся пассажиров этой станции.
        TRY(add_cabin(&station->cabins, cabin->id));
        cabin->state = ON_STATION;
        cabin->doors_open = true;
        TRY(log_event(&service->event_log, service->current_time,
            "✅ Кабина %d подана из резерва на станцию %s", cabin->id, station_name(station->ID_station)));
        TRY(serve_cabin(service, cabin));
    }
    return SUCCESS;
}

// главная собирающая функция: логика всей системы 
Status service_step(Service *service) {
    SOFT_ASSERT(service != NULL && service->cabins != NULL, "Сервис не инициализирован", ERROR);
    if (service->status == SERVICE_FINISHED) return SUCCESS;
    SOFT_ASSERT(service->status != SERVICE_EVACUATING, "Идёт эвакуация", ERROR);

    if (service->current_time >= service->settings.work_time + MAX_DRAIN_TIME) {
        TRY(log_event(&service->event_log, service->current_time,
            "🛑 Превышено время завершения работы; выполняется эвакуация оставшихся пассажиров"));
        return service_evacuate(service);
    }
    TRY(finish_operations(service));
    if (!service->admission_closed && service->current_time >= service->settings.work_time) {
        service->admission_closed = true;
        TRY(log_event(&service->event_log, service->current_time,
            "🛑 Приём новых пассажиров прекращён: время работы вышло"));
    }
    TRY(update_stops(service));
    TRY(arrive_passengers(service));
    if (service->status == SERVICE_RUNNING) {
        for (int index = 0; index < service->settings.cabines; ++index) TRY(serve_cabin(service, &service->cabins[index]));
        for (int index = 0; index < STATION_COUNT; ++index) TRY(admit_cabins(service, &service->stations[index]));
    }
    if (passengers_finished(service) && all_cabins_parked(service)) {
        service->status = SERVICE_FINISHED;
        return SUCCESS;
    }
    SOFT_ASSERT(service->current_time < INT_MAX, "Переполнение модельного времени", ERROR);
    ++service->current_time;
    return SUCCESS;
}
// если прошло много времени с закрытия но пассажиры остались начинаем эвакуацию
Status service_evacuate(Service *service) {
    SOFT_ASSERT(service != NULL && service->cabins != NULL, "Сервис не инициализирован", ERROR);
    service->status = SERVICE_EVACUATING;
    service->admission_closed = true;
    for (int index = 0; index < STATION_COUNT; ++index) {
        Queue *queues[] = {&service->stations[index].vip, &service->stations[index].regular};
        for (int queue_index = 0; queue_index < 2; ++queue_index) {
            while (queues[queue_index]->count) {
                Passenger passenger;
                TRY(take_passenger(queues[queue_index], &passenger));
                ++service->statistics.evacuated;
                TRY(log_event(&service->event_log, service->current_time, "🛑 Пассажир %d эвакуирован со станции %s", passenger.id, station_name(index + 1)));
            }
        }
    }
    for (int index = 0; index < service->settings.cabines; ++index) {
        Cabin *cabin = &service->cabins[index];
        if (cabin->has_boarding_passenger) {
            cabin->has_boarding_passenger = false;
            ++service->statistics.evacuated;
            TRY(log_event(&service->event_log, service->current_time,
                "🛑 Пассажир %d эвакуирован во время посадки в кабину %d", cabin->boarding_passenger.id, cabin->id));
        }
        while (cabin->passengers) {
            Passenger passenger = cabin->passenger_list[--cabin->passengers];
            ++service->statistics.evacuated;
            TRY(log_event(&service->event_log, service->current_time, "🛑 Пассажир %d эвакуирован из кабины %d", passenger.id, cabin->id));
        }
    }
    service->status = SERVICE_FINISHED;
    return log_flush(&service->event_log);
}

// начало работы сервиса
Status service_run(Service *service) {
    SOFT_ASSERT(service != NULL && service->cabins != NULL, "Сервис не инициализирован", ERROR);
    TRY(log_event(&service->event_log, service->current_time, "🧢 Начало работы канатной дороги 🧢"));
    
    while (service->status != SERVICE_FINISHED) {
        TRY(service_step(service));
    }

    TRY(log_event(&service->event_log, service->current_time, "💵 Работа завершена. Принято: %d, доставлено: %d, эвакуировано: %d 💵",
                     service->statistics.accepted, service->statistics.delivered, service->statistics.evacuated));
    return log_flush(&service->event_log);
}
