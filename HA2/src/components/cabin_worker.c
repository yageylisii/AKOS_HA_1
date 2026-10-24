#include "threads/runtime.h"
#include "asserts.h"

#define TRY(expression) do { if ((expression) != SUCCESS) return ERROR; } while (0)

// выполняет отправление после разрешения оператора
static Status depart(Service *service, Station *station, Cabin *cabin) {
    // проверяем закончилась ли посадка
    SOFT_ASSERT(!cabin->has_boarding_passenger, "Посадка ещё не завершена", ERROR);

    // убираем кабину из очереди станции и закрываем двери
    ActorMessage release = {.type = STATION_RELEASE, .cabin_id = cabin->id};
    TRY(actor_request(service, STATION_ACTOR, station->station_id, &release));
    cabin->doors_open = false;

    TRY(log_event(&service->event_log, service->current_time, "🟦 Кабина %d: двери закрыты", cabin->id));
    TRY(log_event(&service->event_log, service->current_time, "📣 Оператор разрешил отправление кабины %d", cabin->id));
    cabin->state = IN_TRANSIT;

    cabin->time_left = service->settings.travel_time; // время поездки
    TRY(log_event(&service->event_log, service->current_time,
        "🚠 Кабина %d движется со станции %s на станцию %s, пассажиров: %d👥",
        cabin->id, station_name(cabin->departure_station), station_name(cabin->destination_station), cabin->passengers));
    return SUCCESS;
}

// завершает текущую операцию кабины по её таймеру
static Status finish_operation(Service *service, Cabin *cabin) {

    if (service->current_time == 0 || service->status != SERVICE_RUNNING){
        return SUCCESS;
    }

    // отсчитываем время и завершаем действие по состоянию кабины
    if (cabin->time_left <= 0){
        return SUCCESS;
    }
    if (--cabin->time_left > 0){
        return SUCCESS;
    }

    if (cabin->state == IN_TRANSIT) { // добавляем кабину в ожидание на станции
        Station *station = &service->stations[cabin->destination_station - 1];
        ActorMessage arrival = {.type = STATION_WAIT, .cabin_id = cabin->id};
        TRY(actor_request(service, STATION_ACTOR, station->station_id, &arrival));
        cabin->state = WAITING_FOR_STATION;
        TRY(log_event(&service->event_log, service->current_time,
            "🚠 Кабина %d закончила путь к станции %s; ожидает допуска, двери закрыты",
            cabin->id, station_name(station->station_id)));

    } else if (cabin->state == UNLOADING) { // пассажир выгружается из кабины
        Passenger passenger = cabin->passenger_list[--cabin->passengers];
        ++service->statistics.delivered;
        TRY(notify_passenger(service, passenger.id,
                             ACTOR_MESSAGE_PASSENGER_DELIVERED,
                             cabin->departure_station, cabin->id,
                             passenger.priority));
        cabin->state = ON_STATION;
        TRY(log_event(&service->event_log, service->current_time,
            "📤 Пассажир %d вышел из кабины %d на станции %s: доставлен",
            passenger.id, cabin->id, station_name(cabin->departure_station)));

    } else if (cabin->state == BOARDING) { // пассажир садится в кабину
        SOFT_ASSERT(cabin->has_boarding_passenger, "Не выбран пассажир для посадки", ERROR);
        Passenger passenger = cabin->boarding_passenger;
        cabin->passenger_list[cabin->passengers++] = passenger;
        cabin->has_boarding_passenger = false;
        TRY(notify_passenger(service, passenger.id,
                             ACTOR_MESSAGE_PASSENGER_BOARDED,
                             cabin->departure_station, cabin->id,
                             passenger.priority));
        TRY(log_event(&service->event_log, service->current_time,
            "📥 Пассажир %d сел в кабину %d", passenger.id, cabin->id));
    }
    return SUCCESS;
}

// выбирает следующую операцию кабины на станции
static Status serve_cabin(Service *service, Cabin *cabin, ActorMessage *reply) {
    // смотрим что кабина на станции
    if (cabin->state != ON_STATION && cabin->state != BOARDING) {
        return SUCCESS;
    }
    if (cabin->time_left > 0) {
        return SUCCESS;
    }

    Station *station = &service->stations[cabin->departure_station - 1];
    // ставим статус высадки когда кабина на станции и имеет пассажиров
    if (cabin->state == ON_STATION && cabin->passengers > 0) {
        cabin->state = UNLOADING;
        cabin->time_left = service->settings.unloading_time;
        return SUCCESS;
    }
    // статус посадки, если есть место в кабине И есть ожидающие на станции
    cabin->state = BOARDING;
    if (cabin->passengers < service->settings.capacity && waiting_passengers(station) > 0) {
        // смотрим чью очередь обслужить (в приоритете VIP)
        ActorMessage choice = {.type = STATION_SELECT};
        TRY(actor_request(service, STATION_ACTOR, station->station_id, &choice));
        cabin->boarding_passenger = (Passenger){.id = choice.entity_id, .priority = choice.priority};
        cabin->has_boarding_passenger = true;
        cabin->time_left = service->settings.boarding_time;
        TRY(notify_passenger(service, cabin->boarding_passenger.id,
                             ACTOR_MESSAGE_PASSENGER_BOARDING_STARTED,
                             station->station_id, cabin->id,
                             cabin->boarding_passenger.priority));
        TRY(log_event(&service->event_log, service->current_time,
            "📥 Пассажир %d начал посадку в кабину %d на станции %s",
            cabin->boarding_passenger.id, cabin->id, station_name(station->station_id)));
        return SUCCESS;
    }
    // после закрытия входа пустые кабины отправляются лишь за ожидающими людьми
    Station *destination = &service->stations[cabin->destination_station - 1];
    if (cabin->passengers == 0 && waiting_passengers(destination) == 0) {
        // освобождаем место для прибывших кабин или ждём следующую группу
        if (!service->admission_closed && station->waiting_cabins.count == 0) return SUCCESS;
        // освобождаем место на стоянке если ожидающих не осталось.
        ActorMessage release = {.type = STATION_RELEASE, .cabin_id = cabin->id};
        TRY(actor_request(service, STATION_ACTOR, station->station_id, &release));
        cabin->state = PARKED;
        cabin->doors_open = false;
        TRY(log_event(&service->event_log, service->current_time,
            "🅿️ Кабина %d выведена на стоянку у станции %s", cabin->id, station_name(station->station_id)));
        return SUCCESS;
    }
    // если кабина уже заполнена а люди остались печатаем лог
    if (cabin->passengers == service->settings.capacity && waiting_passengers(station) > 0)
        TRY(log_event(&service->event_log, service->current_time, "🛑 Кабина %d заполнена: посадка остальных отложена", cabin->id));
    // запрашивает разрешение оператора после завершения операций
    cabin->doors_open = false;
    reply->depart = true;
    return SUCCESS;
}

// эвакуирует пассажиров из кабины включая незавершённую посадку
static Status evacuate_cabin(Service *service, Cabin *cabin) {
    if (cabin->has_boarding_passenger) {
        TRY(notify_passenger(service, cabin->boarding_passenger.id,
                             ACTOR_MESSAGE_PASSENGER_EVACUATED,
                             cabin->departure_station, cabin->id,
                             cabin->boarding_passenger.priority));
        cabin->has_boarding_passenger = false;
        ++service->statistics.evacuated;
        TRY(log_event(&service->event_log, service->current_time,
            "🛑 Пассажир %d эвакуирован во время посадки в кабину %d", cabin->boarding_passenger.id, cabin->id));
    }
    while (cabin->passengers) {
        Passenger passenger = cabin->passenger_list[--cabin->passengers];
        TRY(notify_passenger(service, passenger.id,
                             ACTOR_MESSAGE_PASSENGER_EVACUATED,
                             cabin->departure_station, cabin->id,
                             passenger.priority));
        ++service->statistics.evacuated;
        TRY(log_event(&service->event_log, service->current_time, "🛑 Пассажир %d эвакуирован из кабины %d", passenger.id, cabin->id));
    }
    cabin->time_left = 0;
    return SUCCESS;
}

// выполняет команду кабины и возвращает результат оператору
Status cabin_handle(Service *service, int cabin_id, ActorMessage *message) {
    Cabin *cabin = &service->cabins[cabin_id - 1];
    switch (message->type) {
        case CABIN_EVACUATE:
            return evacuate_cabin(service, cabin);
        case CABIN_TICK:
            return finish_operation(service, cabin);
        case CABIN_SERVE:
            return serve_cabin(service, cabin, message);
        case CABIN_DEPART:
            SOFT_ASSERT(service->status == SERVICE_RUNNING && !cabin->doors_open &&
                        !cabin->has_boarding_passenger && cabin->time_left == 0,
                        "Отправление не разрешено", ERROR);
            return depart(service, &service->stations[cabin->departure_station - 1], cabin);
        case CABIN_ADMIT:
            cabin->departure_station = message->station_id;
            cabin->destination_station = 3 - message->station_id;
            cabin->state = ON_STATION;
            cabin->doors_open = true;
            return SUCCESS;
        default:
            return ERROR;
    }
}

// запускает обработку команд в потоке кабины
void *cabin_worker(void *argument) {
    return actor_work(argument);
}
