#ifndef QUEUE_CABIN_H
#define QUEUE_CABIN_H

#include "components/status.h"

// Храним ID: сама кабина остаётся в массиве Service.cabins.
typedef struct CabinQueueNode {
    int cabin_id;
    struct CabinQueueNode *next;
} CabinQueueNode;

typedef struct CabinQueue {
    CabinQueueNode *head;
    CabinQueueNode *tail;
    int count;
} CabinQueue;

// Начальная инициализация; также допустимо CabinQueue queue = {0}.
// Для очистки непустой очереди использовать cabin_queue_clear().
// Очередь владеет узлами, её нельзя копировать присваиванием.
Status cabin_queue_init(CabinQueue *queue);
Status add_cabin(CabinQueue *queue, int cabin_id);
Status take_cabin(CabinQueue *queue, int *cabin_id);
Status cabin_queue_peek(const CabinQueue *queue, int *cabin_id);
// Удалить указанную кабину, сохраняя порядок остальных.
Status remove_cabin(CabinQueue *queue, int cabin_id);

Status cabin_queue_clear(CabinQueue *queue);

// SUCCESS при успехе, ERROR при неверном указателе, пустой очереди
// для извлечения/просмотра или отказе выделения памяти при добавлении.
// При ошибке очередь и выходной ID не меняются.
// Выходной ID должен храниться вне узлов очереди.
// Очистка освобождает только узлы, не сами кабины.
#endif
