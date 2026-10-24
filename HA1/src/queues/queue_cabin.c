#include "queues/queue_cabin.h"
#include "asserts.h"

#include <limits.h>
#include <stdlib.h>

// создать очередб
Status cabin_queue_init(CabinQueue *queue) {
    SOFT_ASSERT(queue != NULL, "Не передана очередь", ERROR);
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    return SUCCESS;
}

// добавить объект в конец очереди
Status add_cabin(CabinQueue *queue, int cabin_id) {
    SOFT_ASSERT(queue != NULL, "Не передана очередь", ERROR);
    SOFT_ASSERT(queue->count < INT_MAX, "Переполнение счётчика очереди", ERROR);

    CabinQueueNode *node = malloc(sizeof(*node));
    SOFT_ASSERT(node != NULL, "Не удалось выделить память для ID кабины", ERROR);
    node->cabin_id = cabin_id;
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

// взять объект из начала очереди
Status take_cabin(CabinQueue *queue, int *cabin_id) {
    SOFT_ASSERT(queue != NULL, "Не передана очередь", ERROR);
    SOFT_ASSERT(cabin_id != NULL, "Не передан указатель для ID кабины", ERROR);
    SOFT_ASSERT(queue->head != NULL, "Очередь пуста", ERROR);

    CabinQueueNode *node = queue->head;
    *cabin_id = node->cabin_id;
    queue->head = node->next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    queue->count--;
    free(node);
    return SUCCESS;
}

// 
Status cabin_queue_peek(const CabinQueue *queue, int *cabin_id) {
    SOFT_ASSERT(queue != NULL, "Не передана очередь", ERROR);
    SOFT_ASSERT(cabin_id != NULL, "Не передан указатель для ID кабины", ERROR);
    SOFT_ASSERT(queue->head != NULL, "Очередь пуста", ERROR);

    *cabin_id = queue->head->cabin_id;
    return SUCCESS;
}

// очистить
Status cabin_queue_clear(CabinQueue *queue) {
    SOFT_ASSERT(queue != NULL, "Не передана очередь", ERROR);
    CabinQueueNode *node = queue->head;
    while (node != NULL) {
        CabinQueueNode *next = node->next;
        free(node);
        node = next;
    }
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    return SUCCESS;
}

Status remove_cabin(CabinQueue *queue, int cabin_id) {
    SOFT_ASSERT(queue != NULL, "Не передана очередь", ERROR);
    CabinQueueNode *previous = NULL;
    CabinQueueNode *node = queue->head;
    while (node && node->cabin_id != cabin_id) {
        previous = node;
        node = node->next;
    }
    SOFT_ASSERT(node != NULL, "Кабина отсутствует в очереди", ERROR);
    if (previous) previous->next = node->next;
    else queue->head = node->next;
    if (queue->tail == node) queue->tail = previous;
    free(node);
    --queue->count;
    return SUCCESS;
}
