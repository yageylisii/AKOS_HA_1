#include "threads/runtime.h"
#include "asserts.h"

#include <stdlib.h>

// выбирает станцию и билет затем самостоятельно обращается в очередь станции
static Status passenger_arrive(Actor *actor) {
    Service *service = actor->service;
    int priority = rand() / ((double)RAND_MAX + 1.0) < service->settings.vip_probability ? 0 : 1;
    int station_id = rand() % STATION_COUNT + 1;
    ActorMessage request = {.type = STATION_JOIN, .entity_id = actor->id, .priority = priority};
    if (actor_request(service, STATION_ACTOR, station_id, &request) != SUCCESS) return ERROR;
    actor->station_id = station_id;
    actor->state = WAITING;
    ++service->statistics.accepted;
    ++service->next_passenger_id;
    return log_event(&service->event_log, service->current_time,
        "💫 Пассажир %d прибыл на станцию %s (%s)", actor->id,
        station_name(station_id), priority == 0 ? "⚜️ VIP" : "обычный");
}

// проверяет допустимость перехода и подтверждает действие пассажира
Status passenger_handle(Actor *actor, ActorMessage *message) {
    switch (message->type) {
        case ACTOR_MESSAGE_PASSENGER_CREATED:
            if (actor->state != NOT_ARRIVED) return ERROR;
            return passenger_arrive(actor);
        case ACTOR_MESSAGE_PASSENGER_BOARDING_STARTED:
            if (actor->state != WAITING || actor->station_id != message->station_id) return ERROR;
            actor->state = ENTERING;
            actor->cabin_id = message->cabin_id;
            return SUCCESS;
        case ACTOR_MESSAGE_PASSENGER_BOARDED:
            if (actor->state != ENTERING || actor->cabin_id != message->cabin_id) return ERROR;
            actor->state = RIDING;
            actor->station_id = 0;
            return SUCCESS;
        case ACTOR_MESSAGE_PASSENGER_DELIVERED:
            if (actor->state != RIDING || actor->cabin_id != message->cabin_id) return ERROR;
            actor->state = DELIVERED;
            actor->station_id = message->station_id;
            actor->cabin_id = 0;
            return SUCCESS;
        case ACTOR_MESSAGE_PASSENGER_EVACUATED:
            if (actor->state != WAITING && actor->state != ENTERING && actor->state != RIDING) return ERROR;
            actor->state = EVACUATED;
            actor->cabin_id = 0;
            actor->station_id = 0;
            return SUCCESS;
        default:
            return ERROR;
    }
}

// запускает обработку команд пассажира в отдельном потоке
void *passenger_worker(void *argument) {
    return actor_work(argument);
}
