#include "components/queue.h"

#include <stddef.h>

void queue_init(IntQueue *queue)
{
    if (queue == NULL) {
        return;
    }
    queue->head = 0;
    queue->count = 0;
}

bool queue_is_empty(const IntQueue *queue)
{
    return queue == NULL || queue->count == 0;
}

bool queue_push(IntQueue *queue, int value)
{
    if (queue == NULL || queue->count >= QUEUE_CAPACITY) {
        return false;
    }
    int tail = (queue->head + queue->count) % QUEUE_CAPACITY;
    queue->items[tail] = value;
    ++queue->count;
    return true;
}

bool queue_pop(IntQueue *queue, int *value)
{
    if (queue == NULL || queue->count == 0) {
        return false;
    }
    if (value != NULL) {
        *value = queue->items[queue->head];
    }
    queue->head = (queue->head + 1) % QUEUE_CAPACITY;
    --queue->count;
    return true;
}

bool queue_peek(const IntQueue *queue, int *value)
{
    if (queue == NULL || queue->count == 0) {
        return false;
    }
    if (value != NULL) {
        *value = queue->items[queue->head];
    }
    return true;
}

int queue_at(const IntQueue *queue, int index)
{
    if (queue == NULL || index < 0 || index >= queue->count) {
        return -1;
    }
    return queue->items[(queue->head + index) % QUEUE_CAPACITY];
}

bool queue_contains(const IntQueue *queue, int value)
{
    if (queue == NULL) {
        return false;
    }
    for (int index = 0; index < queue->count; ++index) {
        if (queue_at(queue, index) == value) {
            return true;
        }
    }
    return false;
}

// удаляет первое вхождение, сохраняя порядок остальных элементов
bool queue_remove(IntQueue *queue, int value)
{
    if (queue == NULL) {
        return false;
    }
    for (int index = 0; index < queue->count; ++index) {
        if (queue_at(queue, index) != value) {
            continue;
        }
        for (int shift = index; shift + 1 < queue->count; ++shift) {
            int from = (queue->head + shift + 1) % QUEUE_CAPACITY;
            int to = (queue->head + shift) % QUEUE_CAPACITY;
            queue->items[to] = queue->items[from];
        }
        --queue->count;
        return true;
    }
    return false;
}
