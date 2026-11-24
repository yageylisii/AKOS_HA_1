#define _POSIX_C_SOURCE 200809L

#include "ipc/sync.h"
#include "asserts.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

// начальные значения семафоров модели
static unsigned int initial_value(int slot)
{
    return slot == SEM_MODEL ? 1u : 0u;
}
// --------------------
// безымянные семафоры
// --------------------
Status sync_create_unnamed(SyncSet *set, SharedModel *model)
{
    SOFT_ASSERT(set != NULL && model != NULL, "Не передан набор семафоров", ERROR);

    *set = (SyncSet){0};
    set->mode = SYNC_UNNAMED;
    set->owner = true;

    for (int slot = 0; slot < SEM_SLOT_COUNT; ++slot) {
        // pshared = 1: семафор разделяется процессами
        // он лежит в общей памяти, поэтому его видят и потомки, и сторонние процессы
        if (sem_init(&model->semaphores[slot], 1, initial_value(slot)) == -1) {
            perror("sem_init");
            // уничтожаем то, что успели создать
            for (int created = 0; created < slot; ++created) {
                sem_destroy(&model->semaphores[created]);
            }
            return ERROR;
        }
        set->handles[slot] = &model->semaphores[slot];
    }
    return SUCCESS;
}

Status sync_attach_unnamed(SyncSet *set, SharedModel *model)
{
    SOFT_ASSERT(set != NULL && model != NULL, "Не передан набор семафоров", ERROR);

    *set = (SyncSet){0};
    set->mode = SYNC_UNNAMED;
    set->owner = false;
    // отдельная инициализация не нужна: семафоры уже лежат в общей памяти
    for (int slot = 0; slot < SEM_SLOT_COUNT; ++slot) {
        set->handles[slot] = &model->semaphores[slot];
    }
    return SUCCESS;
}

// ---------------------------------------------------------------------------
// именованные семафоры: отличаются по именам вида /akos46_named_0
// ---------------------------------------------------------------------------

static void build_name(char *buffer, size_t size, const char *prefix, int slot)
{
    snprintf(buffer, size, "%s_%d", prefix, slot);
}

static void close_named(SyncSet *set, int count, bool unlink_names)
{
    for (int slot = 0; slot < count; ++slot) {
        if (set->handles[slot] != NULL) {
            sem_close(set->handles[slot]);
            set->handles[slot] = NULL;
        }
        if (unlink_names && set->names[slot][0] != '\0') {
            sem_unlink(set->names[slot]);
        }
    }
}

Status sync_create_named(SyncSet *set, const char *prefix)
{
    SOFT_ASSERT(set != NULL && prefix != NULL, "Не передан префикс семафоров", ERROR);

    *set = (SyncSet){0};
    set->mode = SYNC_NAMED;
    set->owner = true;

    for (int slot = 0; slot < SEM_SLOT_COUNT; ++slot) {
        build_name(set->names[slot], sizeof(set->names[slot]), prefix, slot);

        // флаг O_EXCL отличает новый семафор от оставшегося после прошлого запуска
        sem_t *handle = sem_open(set->names[slot], O_CREAT | O_EXCL, 0600, initial_value(slot));
        if (handle == SEM_FAILED && errno == EEXIST) {
            fprintf(stderr, "Семафор %s остался от прошлого запуска, удаляю\n", set->names[slot]);
            sem_unlink(set->names[slot]);
            handle = sem_open(set->names[slot], O_CREAT | O_EXCL, 0600, initial_value(slot));
        }
        if (handle == SEM_FAILED) {
            perror("sem_open");
            close_named(set, slot, true);
            return ERROR;
        }
        set->handles[slot] = handle;
    }
    return SUCCESS;
}

Status sync_attach_named(SyncSet *set, const char *prefix)
{
    SOFT_ASSERT(set != NULL && prefix != NULL, "Не передан префикс семафоров", ERROR);

    *set = (SyncSet){0};
    set->mode = SYNC_NAMED;
    set->owner = false;

    for (int slot = 0; slot < SEM_SLOT_COUNT; ++slot) {
        build_name(set->names[slot], sizeof(set->names[slot]), prefix, slot);

        // без O_CREAT: открываем только существующий семафор
        sem_t *handle = sem_open(set->names[slot], 0);
        if (handle == SEM_FAILED) {
            // ошибка ENOENT: семафоры ещё не созданы управляющей программой
            if (errno != ENOENT) {
                perror("sem_open");
            }
            close_named(set, slot, false);
            return ERROR;
        }
        set->handles[slot] = handle;
    }
    return SUCCESS;
}

// ---------------------------------------------------------------------------
// операции над семафорами
// ---------------------------------------------------------------------------

Status sync_wait(SyncSet *set, int slot)
{
    SOFT_ASSERT(set != NULL && slot >= 0 && slot < SEM_SLOT_COUNT,
                "Неверный номер семафора", ERROR);
    SOFT_ASSERT(set->handles[slot] != NULL, "Семафор не открыт", ERROR);

    // ожидание прерывается сигналом: по правилам задания программа должна
    // корректно переживать Ctrl+C, поэтому повторяем вызов при EINTR
    while (sem_wait(set->handles[slot]) == -1) {
        if (errno != EINTR) {
            perror("sem_wait");
            return ERROR;
        }
    }
    return SUCCESS;
}

Status sync_post(SyncSet *set, int slot)
{
    SOFT_ASSERT(set != NULL && slot >= 0 && slot < SEM_SLOT_COUNT,
                "Неверный номер семафора", ERROR);
    SOFT_ASSERT(set->handles[slot] != NULL, "Семафор не открыт", ERROR);

    if (sem_post(set->handles[slot]) == -1) {
        perror("sem_post");
        return ERROR;
    }
    return SUCCESS;
}

// приём из примера 04-sem-timewait: sem_timedwait принимает абсолютное
// время, поэтому берём текущее CLOCK_REALTIME и прибавляем срок
// прерывание обработчиком сигнала даёт EINTR — ожидание перезапускается
Status sync_timedwait(SyncSet *set, int slot, int seconds, bool *timed_out)
{
    SOFT_ASSERT(set != NULL && timed_out != NULL && slot >= 0 && slot < SEM_SLOT_COUNT,
                "Неверный номер семафора", ERROR);
    SOFT_ASSERT(set->handles[slot] != NULL, "Семафор не открыт", ERROR);

    *timed_out = false;

    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) == -1) {
        perror("clock_gettime");
        return ERROR;
    }
    deadline.tv_sec += seconds;

    while (sem_timedwait(set->handles[slot], &deadline) == -1) {
        if (errno == ETIMEDOUT) {
            *timed_out = true;
            return SUCCESS;
        }
        if (errno != EINTR) {
            perror("sem_timedwait");
            return ERROR;
        }
    }
    return SUCCESS;
}

Status sync_trywait(SyncSet *set, int slot, bool *acquired)
{
    SOFT_ASSERT(set != NULL && acquired != NULL && slot >= 0 && slot < SEM_SLOT_COUNT,
                "Неверный номер семафора", ERROR);
    SOFT_ASSERT(set->handles[slot] != NULL, "Семафор не открыт", ERROR);

    *acquired = false;
    while (sem_trywait(set->handles[slot]) == -1) {
        if (errno == EAGAIN) {
            return SUCCESS;
        }
        if (errno != EINTR) {
            perror("sem_trywait");
            return ERROR;
        }
    }
    *acquired = true;
    return SUCCESS;
}

void sync_close(SyncSet *set)
{
    if (set == NULL) {
        return;
    }
    // безымянные семафоры закрывать нечем: они исчезнут вместе с отображением
    if (set->mode == SYNC_NAMED) {
        close_named(set, SEM_SLOT_COUNT, false);
    }
    for (int slot = 0; slot < SEM_SLOT_COUNT; ++slot) {
        set->handles[slot] = NULL;
    }
}

void sync_destroy(SyncSet *set)
{
    if (set == NULL) {
        return;
    }
    if (set->mode == SYNC_NAMED) {
        // sem_close в каждом процессе, sem_unlink — только у создателя
        close_named(set, SEM_SLOT_COUNT, set->owner);
    } else if (set->owner) {
        for (int slot = 0; slot < SEM_SLOT_COUNT; ++slot) {
            if (set->handles[slot] != NULL) {
                sem_destroy(set->handles[slot]);
            }
        }
    }
    for (int slot = 0; slot < SEM_SLOT_COUNT; ++slot) {
        set->handles[slot] = NULL;
    }
}
