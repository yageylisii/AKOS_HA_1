#ifndef SYNC_H
#define SYNC_H

#include <semaphore.h>
#include <stdbool.h>

#include "components/model.h"
#include "components/status.h"

// Два вида семафоров POSIX, изученных на семинаре 07.
typedef enum SyncMode {
    SYNC_UNNAMED = 0, // sem_init в разделяемой памяти, pshared = 1
    SYNC_NAMED = 1    // sem_open по имени вида /akos46_...
} SyncMode;

typedef struct SyncSet {
    int mode;                            // SyncMode
    sem_t *handles[SEM_SLOT_COUNT];      // Указатели на семафоры в этом процессе
    char names[SEM_SLOT_COUNT][64];      // Имена, заполняются только в режиме SYNC_NAMED
    bool owner;                          // Процесс обязан удалить семафоры
} SyncSet;

// Неименованные семафоры: размещаются прямо в разделяемой памяти,
// поэтому доступны и связанным процессам после fork, и подключившимся позже.
Status sync_create_unnamed(SyncSet *set, SharedModel *model);
Status sync_attach_unnamed(SyncSet *set, SharedModel *model);

// Именованные семафоры: создаются по имени, наследуются дочерними процессами
// и открываются независимо запущенными программами.
Status sync_create_named(SyncSet *set, const char *prefix);
Status sync_attach_named(SyncSet *set, const char *prefix);

// Ожидание и сигнал с повтором при прерывании сигналом.
Status sync_wait(SyncSet *set, int slot);
Status sync_post(SyncSet *set, int slot);
Status sync_trywait(SyncSet *set, int slot, bool *acquired);

// Ожидание с предельным сроком: sem_timedwait по абсолютному времени
// CLOCK_REALTIME. Нужно там, где участник может не ответить совсем,
// например если его процесс сняли извне. При истечении срока
// возвращает SUCCESS и выставляет timed_out.
Status sync_timedwait(SyncSet *set, int slot, int seconds, bool *timed_out);

// Закрывает дескрипторы семафоров в текущем процессе.
void sync_close(SyncSet *set);

// Закрывает и у владельца окончательно удаляет семафоры
// (sem_destroy для неименованных, sem_unlink для именованных).
void sync_destroy(SyncSet *set);

#endif
