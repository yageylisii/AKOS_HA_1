#ifndef THREADS_RUNTIME_H
#define THREADS_RUNTIME_H

#include "components/service.h"
#include "threads/mailbox.h"

typedef enum ActorKind { STATION_ACTOR, CABIN_ACTOR, PASSENGER_ACTOR } ActorKind;
typedef enum PassengerState { NOT_ARRIVED, WAITING, ENTERING, RIDING, DELIVERED, EVACUATED } PassengerState;

typedef struct Actor {
    int id;
    ActorKind kind;
    Service *service;
    PassengerState state;
    int station_id;
    int cabin_id;
    pthread_t thread;
    Mailbox inbox;
    Mailbox replies;
    bool started;
} Actor;

typedef struct ThreadRuntime {
    Service *service;
    int cabin_count;
    int passenger_limit;
    Actor stations[STATION_COUNT];
    Actor *cabins;
    Actor *passengers;
} ThreadRuntime;

// создаёт потоки станций и кабин для общей модели
Status runtime_init(ThreadRuntime *runtime, Service *service);
// создаёт поток нового пассажира до его появления на станции
Status runtime_start_passenger(ThreadRuntime *runtime, int passenger_id);
// один участник получает не более одного незавершённого запроса
// вызывающий поток не обращается к модели до получения ответа

// передаёт участнику запрос и получает результат перед следующим зависимым действием
Status actor_request(Service *service, ActorKind kind, int actor_id, ActorMessage *message);
// обрабатывает запросы участника и отправляет ответы в его отдельную очередь
void *actor_work(void *argument);
// завершает потоки и освобождает очереди после окончания обмена запросами
void runtime_destroy(ThreadRuntime *runtime);
// запускает обработку команд станции
void *station_worker(void *argument);
// запускает обработку команд кабины
void *cabin_worker(void *argument);
// запускает обработку команд пассажира
void *passenger_worker(void *argument);
// принимает решение станции о пассажирах и допуске кабин
Status station_handle(Service *service, int station_id, ActorMessage *message);
// выполняет операцию кабины или запрашивает разрешение отправления
Status cabin_handle(Service *service, int cabin_id, ActorMessage *message);
// выполняет действия пассажира и проверяет порядок переходов
Status passenger_handle(Actor *actor, ActorMessage *message);
// считает пассажиров в очередях станции
int waiting_passengers(const Station *station);
// получает подтверждение пассажира о переходе в новое состояние
Status notify_passenger(Service *service, int passenger_id, ActorMessageType type,
                        int station_id, int cabin_id, int priority);

#endif
