#include "threads/runtime.h"

#include <stdlib.h>

static Status start_actor(Mailbox *mailbox, pthread_t *thread, bool *started,
                          void *(*worker)(void *), void *argument) {
    if (mailbox_init(mailbox) != SUCCESS) return ERROR;
    if (pthread_create(thread, NULL, worker, argument) != 0) {
        mailbox_destroy(mailbox);
        return ERROR;
    }
    *started = true;
    return SUCCESS;
}

Status runtime_init(ThreadRuntime *runtime, const Settings *settings) {
    if (runtime == NULL || settings == NULL) return ERROR;
    *runtime = (ThreadRuntime){0};
    runtime->cabin_count = settings->cabines;
    runtime->passenger_limit = settings->max_passengers;
    runtime->cabins = calloc((size_t)runtime->cabin_count, sizeof(*runtime->cabins));
    runtime->passengers = calloc((size_t)runtime->passenger_limit, sizeof(*runtime->passengers));
    if (runtime->cabins == NULL ||
        (runtime->passenger_limit > 0 && runtime->passengers == NULL)) {
        runtime_destroy(runtime);
        return ERROR;
    }

    for (int index = 0; index < 2; ++index) {
        StationActor *actor = &runtime->stations[index];
        actor->id = index + 1;
        if (start_actor(&actor->inbox, &actor->thread, &actor->started,
                        station_worker, actor) != SUCCESS) {
            runtime_destroy(runtime);
            return ERROR;
        }
    }
    for (int index = 0; index < runtime->cabin_count; ++index) {
        CabinActor *actor = &runtime->cabins[index];
        actor->id = index + 1;
        actor->capacity = settings->capacity;
        actor->passenger_ids = calloc((size_t)actor->capacity,
                                      sizeof(*actor->passenger_ids));
        if (actor->passenger_ids == NULL) {
            runtime_destroy(runtime);
            return ERROR;
        }
        if (start_actor(&actor->inbox, &actor->thread, &actor->started,
                        cabin_worker, actor) != SUCCESS) {
            runtime_destroy(runtime);
            return ERROR;
        }
    }
    return SUCCESS;
}

Status runtime_start_passenger(ThreadRuntime *runtime, Passenger passenger,
                               int station_id, int model_time) {
    if (runtime == NULL || passenger.id < 1 || passenger.id > runtime->passenger_limit)
        return ERROR;
    PassengerActor *actor = &runtime->passengers[passenger.id - 1];
    if (actor->started) return ERROR;
    actor->id = passenger.id;
    if (start_actor(&actor->inbox, &actor->thread, &actor->started,
                    passenger_worker, actor) != SUCCESS) return ERROR;
    ActorMessage message = {.type=ACTOR_MESSAGE_PASSENGER_CREATED,
        .model_time=model_time,.entity_id=passenger.id,.station_id=station_id,
        .priority=passenger.priority};
    return mailbox_send(&actor->inbox, message);
}

Status runtime_station_event(ThreadRuntime *runtime, int station_id,
                             ActorMessage message) {
    if (runtime == NULL || station_id < 1 || station_id > 2) return ERROR;
    return mailbox_send(&runtime->stations[station_id - 1].inbox, message);
}

Status runtime_cabin_event(ThreadRuntime *runtime, int cabin_id,
                           ActorMessage message) {
    if (runtime == NULL || cabin_id < 1 || cabin_id > runtime->cabin_count) return ERROR;
    return mailbox_send(&runtime->cabins[cabin_id - 1].inbox, message);
}

Status runtime_passenger_event(ThreadRuntime *runtime, int passenger_id,
                               ActorMessage message) {
    if (runtime == NULL || passenger_id < 1 || passenger_id > runtime->passenger_limit)
        return ERROR;
    PassengerActor *actor = &runtime->passengers[passenger_id - 1];
    if (!actor->started) return ERROR;
    return mailbox_send(&actor->inbox, message);
}

static void stop_actor(Mailbox *mailbox, pthread_t thread, bool *started) {
    if (!*started) return;
    mailbox_close(mailbox);
    pthread_join(thread, NULL);
    *started = false;
}

void runtime_destroy(ThreadRuntime *runtime) {
    if (runtime == NULL) return;
    for (int index = 0; runtime->passengers != NULL &&
         index < runtime->passenger_limit; ++index) {
        PassengerActor *actor = &runtime->passengers[index];
        if (actor->started) {
            ActorMessage message = {.type=ACTOR_MESSAGE_STOP,.entity_id=actor->id};
            (void)mailbox_send(&actor->inbox, message);
            stop_actor(&actor->inbox, actor->thread, &actor->started);
        }
        mailbox_destroy(&actor->inbox);
    }
    for (int index = 0; runtime->cabins != NULL && index < runtime->cabin_count; ++index) {
        CabinActor *actor = &runtime->cabins[index];
        if (actor->started) {
            ActorMessage message = {.type=ACTOR_MESSAGE_STOP,.cabin_id=actor->id};
            (void)mailbox_send(&actor->inbox, message);
            stop_actor(&actor->inbox, actor->thread, &actor->started);
        }
        mailbox_destroy(&actor->inbox);
        free(actor->passenger_ids);
    }
    for (int index = 0; index < 2; ++index) {
        StationActor *actor = &runtime->stations[index];
        if (actor->started) {
            ActorMessage message = {.type=ACTOR_MESSAGE_STOP,.station_id=actor->id};
            (void)mailbox_send(&actor->inbox, message);
            stop_actor(&actor->inbox, actor->thread, &actor->started);
        }
        mailbox_destroy(&actor->inbox);
    }
    free(runtime->passengers);
    free(runtime->cabins);
    *runtime = (ThreadRuntime){0};
}
