#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>

#include "components/settings.h"

// очереди живут в разделяемой памяти, поэтому списки на указателях из HA1
// заменены кольцевым буфером фиксированного размера: адреса в разных
// процессах не совпадают, а индексы одинаковы для всех участников
enum { QUEUE_CAPACITY = MAX_PASSENGERS };

typedef struct IntQueue {
    int items[QUEUE_CAPACITY]; // номера пассажиров или индексы кабин
    int head;                  // позиция первого элемента
    int count;                 // текущая длина очереди
} IntQueue;

void queue_init(IntQueue *queue);
bool queue_is_empty(const IntQueue *queue);
bool queue_push(IntQueue *queue, int value);     // в хвост; false при переполнении
bool queue_pop(IntQueue *queue, int *value);     // из головы; false если пусто
bool queue_peek(const IntQueue *queue, int *value);
bool queue_remove(IntQueue *queue, int value);   // удаляет первое вхождение
bool queue_contains(const IntQueue *queue, int value);
int queue_at(const IntQueue *queue, int index);  // элемент по порядковому номеру

#endif
