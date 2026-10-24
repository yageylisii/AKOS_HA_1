#include "threads/runtime.h"
#include "asserts.h"

#define TRY(expression) do { if ((expression) != SUCCESS) return ERROR; } while (0)

// считает пассажиров в очередях станции
int waiting_passengers(const Station *station) {
    return station->vip.count + station->regular.count;
}

// выбирает следующую кабину с приоритетом прибывших перед резервными
static Status admit_next(Service *service, Station *station, ActorMessage *reply) {
    reply->cabin_id = 0;
    reply->station_id = station->station_id;
    if (station->cabins.count >= STATION_CABIN_CAPACITY) return SUCCESS;
    int cabin_id = 0;
    bool from_reserve = false;
    if (station->waiting_cabins.count > 0) {
        TRY(cabin_queue_peek(&station->waiting_cabins, &cabin_id));
    } else if (waiting_passengers(station) > 0) {
        for (int index = 0; index < service->settings.cabin_count; ++index) {
            Cabin *cabin = &service->cabins[index];
            if (cabin->state == PARKED && cabin->departure_station == station->station_id) {
                cabin_id = cabin->id;
                from_reserve = true;
                break;
            }
        }
    }
    if (cabin_id == 0) return SUCCESS;
    TRY(add_cabin(&station->cabins, cabin_id));
    if (!from_reserve) TRY(take_cabin(&station->waiting_cabins, &cabin_id));
    reply->cabin_id = cabin_id;
    return log_event(&service->event_log, service->current_time,
        from_reserve ? "✅ Кабина %d подана из резерва на станцию %s"
                     : "✅ Кабина %d прибыла на станцию %s, двери открыты",
        cabin_id, station_name(station->station_id));
}

// эвакуирует пассажиров из очередей станции с подтверждением каждого участника
static Status evacuate_station(Service *service, int station_id) {
    Queue *queues[] = {&service->stations[station_id - 1].vip, &service->stations[station_id - 1].regular};
    for (int queue_index = 0; queue_index < 2; ++queue_index) {
        while (queues[queue_index]->count) {
            Passenger passenger;
            TRY(take_passenger(queues[queue_index], &passenger));
            TRY(notify_passenger(service, passenger.id,
                                 ACTOR_MESSAGE_PASSENGER_EVACUATED,
                                 station_id, 0, passenger.priority));
            ++service->statistics.evacuated;
            TRY(log_event(&service->event_log, service->current_time, "🛑 Пассажир %d эвакуирован со станции %s", passenger.id, station_name(station_id)));
        }
    }
    return SUCCESS;
}

// принимает решения станции о очередях пассажиров и допуске кабин
Status station_handle(Service *service, int station_id, ActorMessage *message) {
    Station *station = &service->stations[station_id - 1];
    Passenger passenger = {.id = message->entity_id, .priority = message->priority};
    switch (message->type) {
        case STATION_JOIN:
            return add_passenger(passenger.priority == 0 ? &station->vip : &station->regular, passenger);
        case STATION_SELECT: {
            Queue *queue = station->vip.count > 0 ? &station->vip : &station->regular;
            TRY(take_passenger(queue, &passenger));
            message->entity_id = passenger.id;
            message->priority = passenger.priority;
            return SUCCESS;
        }
        case STATION_EVACUATE:
            return evacuate_station(service, station_id);
        case STATION_ADMIT:
            return admit_next(service, station, message);
        case STATION_WAIT:
            return add_cabin(&station->waiting_cabins, message->cabin_id);
        case STATION_RELEASE:
            return remove_cabin(&station->cabins, message->cabin_id);
        default:
            return ERROR;
    }
}

// запускает обработку команд станции в отдельном потоке
void *station_worker(void *argument) {
    return actor_work(argument);
}
