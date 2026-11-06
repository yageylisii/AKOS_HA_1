#define _POSIX_C_SOURCE 200809L

#include "roles/standalone.h"
#include "asserts.h"
#include "ipc/ipc_names.h"
#include "roles/clock.h"

#include <stdio.h>
#include <time.h>

// Сколько участник ждёт появления ресурсов, если его запустили
// раньше управляющей программы, и пауза между попытками.
enum { ATTACH_ATTEMPTS = 60, ATTACH_DELAY_MS = 500 };

static void sleep_ms(int milliseconds)
{
    struct timespec remaining = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (long)(milliseconds % 1000) * 1000000L
    };
    // Прерывание сигналом здесь не мешает: выходим из паузы досрочно.
    nanosleep(&remaining, &remaining);
}

// Подключение к ресурсам, созданным управляющей программой.
// Как в семинарском примере с читателем и писателем, порядок запуска
// не фиксирован: участник, стартовавший раньше, ждёт появления сегмента.
Status standalone_attach(StandaloneLink *link)
{
    SOFT_ASSERT(link != NULL, "Не передано подключение", ERROR);

    bool reported = false;
    for (int attempt = 0; attempt < ATTACH_ATTEMPTS; ++attempt) {
        if (shared_memory_attach(&link->memory, SHM_NAME_STANDALONE,
                                 sizeof(SharedModel)) == SUCCESS) {
            link->model = (SharedModel *)link->memory.address;

            if (sync_attach_named(&link->sync, SEM_PREFIX_STANDALONE) == SUCCESS) {
                return SUCCESS;
            }
            // Сегмент уже есть, а семафоры ещё не созданы: ждём дальше.
            shared_memory_detach(&link->memory);
            link->model = NULL;
        }

        if (signal_received()) {
            fprintf(stderr, "Ожидание прервано пользователем\n");
            return ERROR;
        }
        if (!reported) {
            fprintf(stderr, "Жду запуска управляющей программы cc_control...\n");
            reported = true;
        }
        sleep_ms(ATTACH_DELAY_MS);
    }

    fprintf(stderr, "Управляющая программа cc_control так и не запустилась\n");
    return ERROR;
}

void standalone_detach(StandaloneLink *link)
{
    if (link == NULL) {
        return;
    }
    sync_close(&link->sync);
    shared_memory_detach(&link->memory);
    link->model = NULL;
}

// Сообщает управляющей программе о своём подключении.
Status standalone_announce(StandaloneLink *link)
{
    SOFT_ASSERT(link != NULL && link->model != NULL, "Нет подключения к модели", ERROR);
    return sync_post(&link->sync, SEM_REGISTERED);
}

// Ожидание разрешения на запуск. Приём из семинарского примера:
// участник блокируется на семафоре администратора и сразу поднимает его
// обратно, пропуская следующего. Опрос состояния в цикле не нужен.
Status standalone_wait_start(StandaloneLink *link)
{
    SOFT_ASSERT(link != NULL && link->model != NULL, "Нет подключения к модели", ERROR);

    TRY(sync_wait(&link->sync, SEM_ADMIN));
    TRY(sync_post(&link->sync, SEM_ADMIN));

    // Управляющая программа могла поднять семафор не ради старта,
    // а чтобы выпустить участников при отказе от запуска.
    TRY(sync_wait(&link->sync, SEM_MODEL));
    bool allowed = link->model->start_allowed && !link->model->shutdown;
    TRY(sync_post(&link->sync, SEM_MODEL));

    SOFT_ASSERT(allowed, "Моделирование не было запущено", ERROR);
    return SUCCESS;
}
