#include "threads/runtime.h"

void *cabin_worker(void *argument) {
    CabinActor *actor = argument;
    ActorMessage message;
    while (mailbox_receive(&actor->inbox, &message) == SUCCESS) {
        if (message.type == ACTOR_MESSAGE_STOP) break;
        switch (message.type) {
            case ACTOR_MESSAGE_CABIN_ADMITTED:
                actor->current_station = message.station_id;
                actor->stopped = false;
                actor->state = 1;
                break;
            case ACTOR_MESSAGE_CABIN_DEPARTED:
                actor->current_station = 0;
                actor->state = 2;
                break;
            case ACTOR_MESSAGE_CABIN_BOARDING_STARTED:
                actor->state = 3;
                break;
            case ACTOR_MESSAGE_CABIN_UNLOADING_STARTED:
                actor->state = 4;
                break;
            case ACTOR_MESSAGE_CABIN_BOARDING_FINISHED:
                if (actor->passengers < actor->capacity) {
                    actor->passenger_ids[actor->passengers++] = message.entity_id;
                }
                break;
            case ACTOR_MESSAGE_CABIN_UNLOADING_FINISHED:
                for (int index = actor->passengers - 1; index >= 0; --index) {
                    if (actor->passenger_ids[index] == message.entity_id) {
                        for (int move = index + 1; move < actor->passengers; ++move)
                            actor->passenger_ids[move - 1] = actor->passenger_ids[move];
                        --actor->passengers;
                        break;
                    }
                }
                break;
            case ACTOR_MESSAGE_CABIN_PASSENGER_EVACUATED:
                for (int index = actor->passengers - 1; index >= 0; --index) {
                    if (actor->passenger_ids[index] == message.entity_id) {
                        for (int move = index + 1; move < actor->passengers; ++move)
                            actor->passenger_ids[move - 1] = actor->passenger_ids[move];
                        --actor->passengers;
                        break;
                    }
                }
                break;
            case ACTOR_MESSAGE_CABIN_TRIP_FINISHED:
                actor->current_station = message.station_id;
                actor->state = 5;
                break;
            case ACTOR_MESSAGE_STOP_STARTED:
                actor->stopped = true;
                break;
            case ACTOR_MESSAGE_STOP_FINISHED:
                actor->stopped = false;
                break;
            case ACTOR_MESSAGE_CABIN_PARKED:
                actor->current_station = message.station_id;
                actor->state = 6;
                break;
            default:
                break;
        }
    }
    return NULL;
}
