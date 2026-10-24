#include "threads/runtime.h"

void *passenger_worker(void *argument) {
    PassengerActor *actor = argument;
    ActorMessage message;
    while (mailbox_receive(&actor->inbox, &message) == SUCCESS) {
        if (message.type == ACTOR_MESSAGE_PASSENGER_CREATED) {
            actor->state = 1; /* ожидает на станции */
            actor->priority = message.priority;
            actor->station_id = message.station_id;
        } else if (message.type == ACTOR_MESSAGE_PASSENGER_BOARDING_STARTED) {
            actor->state = 2; /* началась посадка */
            actor->cabin_id = message.cabin_id;
        } else if (message.type == ACTOR_MESSAGE_PASSENGER_BOARDED) {
            actor->state = 3; /* находится в кабине */
        } else if (message.type == ACTOR_MESSAGE_PASSENGER_DELIVERED) {
            actor->state = 4; /* доставлен */
            actor->station_id = message.station_id;
        } else if (message.type == ACTOR_MESSAGE_PASSENGER_EVACUATED) {
            actor->state = 5; /* эвакуирован */
        }
        if (message.type == ACTOR_MESSAGE_PASSENGER_DELIVERED ||
            message.type == ACTOR_MESSAGE_PASSENGER_EVACUATED ||
            message.type == ACTOR_MESSAGE_STOP) break;
    }
    return NULL;
}
