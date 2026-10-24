#ifndef THREADS_MESSAGE_H
#define THREADS_MESSAGE_H

#include <stdbool.h>
#include "components/status.h"

typedef enum ActorMessageType {
    CABIN_EVACUATE,
    STATION_EVACUATE,
    CABIN_TICK,
    CABIN_SERVE,
    CABIN_ADMIT,
    CABIN_DEPART,
    STATION_ADMIT,
    STATION_JOIN,
    STATION_SELECT,
    STATION_WAIT,
    STATION_RELEASE,
    ACTOR_MESSAGE_PASSENGER_CREATED,
    ACTOR_MESSAGE_PASSENGER_BOARDING_STARTED,
    ACTOR_MESSAGE_PASSENGER_BOARDED,
    ACTOR_MESSAGE_PASSENGER_DELIVERED,
    ACTOR_MESSAGE_PASSENGER_EVACUATED
} ActorMessageType;

typedef struct ActorMessage {
    ActorMessageType type;
    int entity_id;
    int station_id;
    int cabin_id;
    int priority;
    bool depart;
    Status result;
} ActorMessage;

#endif
