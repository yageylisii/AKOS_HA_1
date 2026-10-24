#ifndef THREADS_RUNTIME_H
#define THREADS_RUNTIME_H

#include <pthread.h>
#include <stdbool.h>

#include "components/passenger.h"
#include "components/settings.h"
#include "components/status.h"
#include "threads/mailbox.h"

typedef struct StationActor {
    int id;
    int waiting_vip;
    int waiting_regular;
    int cabins_on_station;
    int cabin_ids[3];
    int waiting_cabin_ids[MAX_CABINS];
    int waiting_cabins;
    int vip_ids[MAX_PASSENGERS];
    int regular_ids[MAX_PASSENGERS];
    int vip_head;
    int regular_head;
    pthread_t thread;
    Mailbox inbox;
    bool started;
} StationActor;

typedef struct CabinActor {
    int id;
    int passengers;
    int capacity;
    int *passenger_ids;
    int current_station;
    bool stopped;
    int state;
    pthread_t thread;
    Mailbox inbox;
    bool started;
} CabinActor;

typedef struct PassengerActor {
    int id;
    int priority;
    int station_id;
    int cabin_id;
    int state;
    pthread_t thread;
    Mailbox inbox;
    bool started;
} PassengerActor;

typedef struct ThreadRuntime {
    int cabin_count;
    int passenger_limit;
    StationActor stations[2];
    CabinActor *cabins;
    PassengerActor *passengers;
} ThreadRuntime;

void *station_worker(void *argument);
void *cabin_worker(void *argument);
void *passenger_worker(void *argument);

Status runtime_init(ThreadRuntime *runtime, const Settings *settings);
Status runtime_start_passenger(ThreadRuntime *runtime, Passenger passenger,
                               int station_id, int model_time);
Status runtime_station_event(ThreadRuntime *runtime, int station_id,
                             ActorMessage message);
Status runtime_cabin_event(ThreadRuntime *runtime, int cabin_id,
                           ActorMessage message);
Status runtime_passenger_event(ThreadRuntime *runtime, int passenger_id,
                               ActorMessage message);
void runtime_destroy(ThreadRuntime *runtime);

#endif
