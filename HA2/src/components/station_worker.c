#include "threads/runtime.h"

static void append_passenger(StationActor *actor, ActorMessage message) {
    int *ids = message.priority == 0 ? actor->vip_ids : actor->regular_ids;
    int head = message.priority == 0 ? actor->vip_head : actor->regular_head;
    int *count = message.priority == 0 ? &actor->waiting_vip : &actor->waiting_regular;
    if (*count >= MAX_PASSENGERS) return;
    ids[(head + *count) % MAX_PASSENGERS] = message.entity_id;
    ++*count;
}

static void remove_passenger(StationActor *actor, ActorMessage message) {
    bool vip = message.priority == 0;
    int *ids = vip ? actor->vip_ids : actor->regular_ids;
    int *head = vip ? &actor->vip_head : &actor->regular_head;
    int *count = vip ? &actor->waiting_vip : &actor->waiting_regular;
    for (int offset = 0; offset < *count; ++offset) {
        if (ids[(*head + offset) % MAX_PASSENGERS] != message.entity_id) continue;
        for (int shift = offset; shift + 1 < *count; ++shift)
            ids[(*head + shift) % MAX_PASSENGERS] =
                ids[(*head + shift + 1) % MAX_PASSENGERS];
        --*count;
        return;
    }
}

static void remove_cabin_slot(StationActor *actor, int cabin_id) {
    for (int index = 0; index < 3; ++index) {
        if (actor->cabin_ids[index] == cabin_id) {
            actor->cabin_ids[index] = 0;
            if (actor->cabins_on_station > 0) --actor->cabins_on_station;
            return;
        }
    }
}

static void remove_waiting_cabin(StationActor *actor, int cabin_id) {
    for (int index = 0; index < actor->waiting_cabins; ++index) {
        if (actor->waiting_cabin_ids[index] != cabin_id) continue;
        for (int move = index + 1; move < actor->waiting_cabins; ++move)
            actor->waiting_cabin_ids[move - 1] = actor->waiting_cabin_ids[move];
        --actor->waiting_cabins;
        return;
    }
}

void *station_worker(void *argument) {
    StationActor *actor = argument;
    ActorMessage message;
    while (mailbox_receive(&actor->inbox, &message) == SUCCESS) {
        if (message.type == ACTOR_MESSAGE_STOP) break;
        switch (message.type) {
            case ACTOR_MESSAGE_PASSENGER_CREATED:
                append_passenger(actor, message);
                break;
            case ACTOR_MESSAGE_PASSENGER_BOARDING_STARTED:
                remove_passenger(actor, message);
                break;
            case ACTOR_MESSAGE_PASSENGER_EVACUATED:
                remove_passenger(actor, message);
                break;
            case ACTOR_MESSAGE_CABIN_WAITING:
                if (actor->waiting_cabins < MAX_CABINS)
                    actor->waiting_cabin_ids[actor->waiting_cabins++] = message.cabin_id;
                break;
            case ACTOR_MESSAGE_CABIN_ADMITTED:
                remove_waiting_cabin(actor, message.cabin_id);
                for (int index = 0; index < 3; ++index) {
                    if (actor->cabin_ids[index] == 0) {
                        actor->cabin_ids[index] = message.cabin_id;
                        ++actor->cabins_on_station;
                        break;
                    }
                }
                break;
            case ACTOR_MESSAGE_CABIN_DEPARTED:
                remove_cabin_slot(actor, message.cabin_id);
                break;
            case ACTOR_MESSAGE_CABIN_PARKED:
                remove_cabin_slot(actor, message.cabin_id);
                break;
            default:
                break;
        }
    }
    return NULL;
}
