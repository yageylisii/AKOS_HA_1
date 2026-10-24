#include "threads/runtime.h"

#include <stdlib.h>

// создаёт очереди запросов и ответов и запускает поток участника
static Status start_actor(Actor *actor, Service *service, ActorKind kind, int actor_id) {
    actor->service = service;
    actor->kind = kind;
    actor->id = actor_id;
    if (mailbox_init(&actor->inbox) != SUCCESS) return ERROR;
    if (mailbox_init(&actor->replies) != SUCCESS) {
        mailbox_destroy(&actor->inbox);
        return ERROR;
    }
    void *(*worker)(void *) = passenger_worker;
    if (kind == STATION_ACTOR) worker = station_worker;
    if (kind == CABIN_ACTOR) worker = cabin_worker;
    if (pthread_create(&actor->thread, NULL, worker, actor) != 0) {
        mailbox_destroy(&actor->inbox);
        mailbox_destroy(&actor->replies);
        return ERROR;
    }
    actor->started = true;
    return SUCCESS;
}

// создаёт потоки станций и кабин для общей модели
Status runtime_init(ThreadRuntime *runtime, Service *service) {
    if (runtime == NULL || service == NULL) return ERROR;
    *runtime = (ThreadRuntime){.service = service};
    runtime->cabin_count = service->settings.cabin_count;
    runtime->passenger_limit = service->settings.max_passengers;
    runtime->cabins = calloc((size_t)runtime->cabin_count, sizeof(Actor));
    runtime->passengers = calloc((size_t)runtime->passenger_limit, sizeof(Actor));
    if (runtime->cabins == NULL || (runtime->passenger_limit > 0 && runtime->passengers == NULL)) {
        runtime_destroy(runtime);
        return ERROR;
    }
    for (int index = 0; index < STATION_COUNT; ++index) {
        if (start_actor(&runtime->stations[index], service, STATION_ACTOR, index + 1) != SUCCESS) {
            runtime_destroy(runtime);
            return ERROR;
        }
    }
    for (int index = 0; index < runtime->cabin_count; ++index) {
        if (start_actor(&runtime->cabins[index], service, CABIN_ACTOR, index + 1) != SUCCESS) {
            runtime_destroy(runtime);
            return ERROR;
        }
    }
    return SUCCESS;
}

// создаёт поток нового пассажира до его появления на станции
Status runtime_start_passenger(ThreadRuntime *runtime, int passenger_id) {
    if (runtime == NULL || passenger_id < 1 || passenger_id > runtime->passenger_limit) return ERROR;
    Actor *actor = &runtime->passengers[passenger_id - 1];
    if (actor->started) return ERROR;
    return start_actor(actor, runtime->service, PASSENGER_ACTOR, passenger_id);
}

// выполняет обработчик того участника которому принадлежит запрос
static Status handle_message(Actor *actor, ActorMessage *message) {
    switch (actor->kind) {
        case STATION_ACTOR: return station_handle(actor->service, actor->id, message);
        case CABIN_ACTOR: return cabin_handle(actor->service, actor->id, message);
        case PASSENGER_ACTOR: return passenger_handle(actor, message);
        default: return ERROR;
    }
}

// обрабатывает запросы участника и отправляет ответы в его отдельную очередь
void *actor_work(void *argument) {
    Actor *actor = argument;
    ActorMessage message;
    while (mailbox_receive(&actor->inbox, &message) == SUCCESS) {
        message.result = handle_message(actor, &message);
        if (mailbox_send(&actor->replies, message) != SUCCESS) break;
    }
    // закрытие будит вызывающий поток даже при ошибке выделения памяти для ответа
    mailbox_close(&actor->replies);
    return NULL;
}

// передаёт запрос владельцу данных и ждёт завершения перед следующим действием
Status actor_request(Service *service, ActorKind kind, int actor_id, ActorMessage *message) {
    if (service == NULL || message == NULL || actor_id < 1) return ERROR;
    if (kind == STATION_ACTOR && actor_id > STATION_COUNT) return ERROR;
    if (kind == CABIN_ACTOR && actor_id > service->settings.cabin_count) return ERROR;
    if (kind != STATION_ACTOR && kind != CABIN_ACTOR && kind != PASSENGER_ACTOR) return ERROR;
    if (service->runtime == NULL) {
        // пошаговые тесты используют те же правила без создания системных потоков
        Actor actor = {.service = service, .kind = kind, .id = actor_id};
        if (kind == PASSENGER_ACTOR && message->type != ACTOR_MESSAGE_PASSENGER_CREATED) return SUCCESS;
        return handle_message(&actor, message);
    }
    ThreadRuntime *runtime = service->runtime;
    Actor *actor;
    if (kind == STATION_ACTOR) actor = &runtime->stations[actor_id - 1];
    else if (kind == CABIN_ACTOR) actor = &runtime->cabins[actor_id - 1];
    else {
        if (actor_id > runtime->passenger_limit) return ERROR;
        actor = &runtime->passengers[actor_id - 1];
    }
    if (!actor->started) return ERROR;
    if (mailbox_send(&actor->inbox, *message) != SUCCESS) return ERROR;
    if (mailbox_receive(&actor->replies, message) != SUCCESS) return ERROR;
    return message->result;
}

// получает подтверждение пассажира о переходе в новое состояние
Status notify_passenger(Service *service, int passenger_id, ActorMessageType type,
                        int station_id, int cabin_id, int priority) {
    ActorMessage message = {.type = type, .station_id = station_id,
        .cabin_id = cabin_id, .priority = priority, .entity_id = passenger_id};
    return actor_request(service, PASSENGER_ACTOR, passenger_id, &message);
}

// закрывает запросы и присоединяет поток перед удалением его очередей
static void stop_actor(Actor *actor) {
    if (actor->started) {
        mailbox_close(&actor->inbox);
        pthread_join(actor->thread, NULL);
        actor->started = false;
    }
    mailbox_destroy(&actor->inbox);
    mailbox_destroy(&actor->replies);
}

// завершает потоки после возврата всех отправленных запросов
void runtime_destroy(ThreadRuntime *runtime) {
    if (runtime == NULL) return;
    for (int index = 0; runtime->passengers != NULL && index < runtime->passenger_limit; ++index)
        stop_actor(&runtime->passengers[index]);
    for (int index = 0; runtime->cabins != NULL && index < runtime->cabin_count; ++index)
        stop_actor(&runtime->cabins[index]);
    for (int index = 0; index < STATION_COUNT; ++index) stop_actor(&runtime->stations[index]);
    free(runtime->passengers);
    free(runtime->cabins);
    *runtime = (ThreadRuntime){0};
}
