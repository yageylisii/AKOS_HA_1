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

// создаёт пустую очередь кабин
Status cabin_queue_init(CabinQueue *queue);
// добавляет кабину в конец очереди
Status add_cabin(CabinQueue *queue, int cabin_id);
// извлекает первую кабину из очереди
Status take_cabin(CabinQueue *queue, int *cabin_id);
// возвращает номер первой кабины без удаления из очереди
Status cabin_queue_peek(const CabinQueue *queue, int *cabin_id);
// удаляет кабину с указанным номером из очереди
Status remove_cabin(CabinQueue *queue, int cabin_id);

// освобождает все узлы очереди кабин
Status cabin_queue_clear(CabinQueue *queue);

// SUCCESS при успехе, ERROR при неверном указателе, пустой очереди
// для извлечения/просмотра или отказе выделения памяти при добавлении.
// При ошибке очередь и выходной ID не меняются.
// Выходной ID должен храниться вне узлов очереди.
// Очистка освобождает только узлы, не сами кабины.
#endif
