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

// Перед использованием вызвать queue_init() или задать Queue queue = {0}.
// Не вызывать для непустой очереди: для её очистки использовать queue_clear().
// Инициализированную очередь нельзя копировать присваиванием: узлы принадлежат ей.
Status queue_init(Queue *queue);

// Добавить копию пассажира в конец. ERROR при отказе malloc или неверном указателе.
Status add_passenger(Queue *queue, Passenger passenger);

// Извлечь первого пассажира и освободить его узел.
Status take_passenger(Queue *queue, Passenger *passenger);

// Получить копию первого пассажира без удаления.
Status queue_peek(const Queue *queue, Passenger *passenger);

// Освободить все узлы. После очистки очередь можно использовать снова.
Status queue_clear(Queue *queue);

// Все операции возвращают SUCCESS или ERROR.
// Для take_passenger/queue_peek выходной пассажир должен храниться вне узлов очереди.
// При ошибке очередь и выходной пассажир не изменяются.
#endif
