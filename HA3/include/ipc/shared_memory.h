#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <stdbool.h>
#include <stddef.h>

#include "components/status.h"

// разделяемая память стандарта POSIX: shm_open + ftruncate + mmap
typedef struct SharedMemory {
    char name[64];  // имя сегмента вида /akos46_<суффикс>
    void *address;  // адрес отображения в этом процессе
    size_t size;    // размер сегмента
    bool owner;     // true у процесса, который создал сегмент и обязан его удалить
} SharedMemory;

// создаёт новый сегмент. Если сегмент с таким именем остался от прошлого
// запуска, он удаляется: O_EXCL отличает чужой живой сегмент от мусора
Status shared_memory_create(SharedMemory *memory, const char *name, size_t size);

// подключается к уже созданному сегменту. Применяется независимыми программами
Status shared_memory_attach(SharedMemory *memory, const char *name, size_t size);

// отключает отображение. Имя сегмента остаётся в системе
void shared_memory_detach(SharedMemory *memory);

// отключает отображение и у владельца удаляет имя через shm_unlink
void shared_memory_destroy(SharedMemory *memory);

#endif
