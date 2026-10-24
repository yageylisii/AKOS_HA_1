#ifndef QUEUE_PASSENGER_H
#define QUEUE_PASSENGER_H

#include "components/passenger.h"
#include "components/status.h"

typedef struct QueueNode {
    Passenger passenger;
    struct QueueNode *next;
} QueueNode;

typedef struct Queue {
    QueueNode *head; // Первый пассажир
    QueueNode *tail; // Последний пассажир
    int count;
} Queue;

// перед использованием вызвать queue_init или обнулить Queue
// непустую очередь сначала очистить через queue_clear
// очередь нельзя копировать присваиванием поскольку она владеет своими узлами

// создаёт пустую очередь пассажиров
Status queue_init(Queue *queue);

// добавляет пассажира в конец очереди
Status add_passenger(Queue *queue, Passenger passenger);

// извлекает первого пассажира из очереди
Status take_passenger(Queue *queue, Passenger *passenger);

// возвращает первого пассажира без удаления из очереди
Status queue_peek(const Queue *queue, Passenger *passenger);

// освобождает все узлы очереди пассажиров
Status queue_clear(Queue *queue);

// Все операции возвращают SUCCESS или ERROR.
// Для take_passenger/queue_peek выходной пассажир должен храниться вне узлов очереди.
// При ошибке очередь и выходной пассажир не изменяются.
#endif
