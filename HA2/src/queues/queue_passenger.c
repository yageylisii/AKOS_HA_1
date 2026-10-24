#include "queues/queue_passenger.h"
#include "asserts.h"

#include <limits.h>
#include <stdlib.h>

// создаёт пустую очередь пассажиров
Status queue_init(Queue *queue) {
    SOFT_ASSERT(queue != NULL, "Не передана очередь", ERROR);
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    return SUCCESS;
}

// добавляет пассажира в конец очереди
Status add_passenger(Queue *queue, Passenger passenger) {
    SOFT_ASSERT(queue != NULL, "Не передана очередь", ERROR);
    SOFT_ASSERT(queue->count < INT_MAX, "Переполнение счётчика очереди", ERROR);

    QueueNode *node = malloc(sizeof(*node));
    SOFT_ASSERT(node != NULL, "Не удалось выделить память для пассажира", ERROR);
    node->passenger = passenger;
    node->next = NULL;

    if (queue->tail != NULL) {
        queue->tail->next = node;
    } else {
        queue->head = node;
    }
    queue->tail = node;
    queue->count++;
    return SUCCESS;
}

// извлекает первого пассажира из очереди
Status take_passenger(Queue *queue, Passenger *passenger) {
    SOFT_ASSERT(queue != NULL, "Не передана очередь", ERROR);
    SOFT_ASSERT(passenger != NULL, "Не передан указатель для пассажира", ERROR);
    SOFT_ASSERT(queue->head != NULL, "Очередь пуста", ERROR);

    QueueNode *node = queue->head;
    *passenger = node->passenger;
    queue->head = node->next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    queue->count--;
    free(node);
    return SUCCESS;
}

// возвращает первого пассажира без удаления из очереди
Status queue_peek(const Queue *queue, Passenger *passenger) {
    SOFT_ASSERT(queue != NULL, "Не передана очередь", ERROR);
    SOFT_ASSERT(passenger != NULL, "Не передан указатель для пассажира", ERROR);
    SOFT_ASSERT(queue->head != NULL, "Очередь пуста", ERROR);

    *passenger = queue->head->passenger;
    return SUCCESS;
}

// освобождает все узлы очереди пассажиров
Status queue_clear(Queue *queue) {
    SOFT_ASSERT(queue != NULL, "Не передана очередь", ERROR);
    QueueNode *node = queue->head;
    while (node != NULL) {
        QueueNode *next = node->next;
        free(node);
        node = next;
    }
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    return SUCCESS;
}
