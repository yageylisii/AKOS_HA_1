// Вариант на оценку 7-8 баллов: программа одной кабины.
// Запускается независимо столько раз, сколько кабин задано настройками.
// Каждый экземпляр занимает первый свободный номер кабины в модели.

#define _POSIX_C_SOURCE 200809L

#include "roles/clock.h"
#include "roles/roles.h"
#include "roles/standalone.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Занимаем свободный номер кабины. Номер можно задать явно аргументом.
static int register_cabin(StandaloneLink *link, int requested)
{
    if (sync_wait(&link->sync, SEM_MODEL) != SUCCESS) {
        return -1;
    }

    SharedModel *model = link->model;
    int chosen = -1;

    if (requested > 0) {
        if (requested > model->cabin_count) {
            fprintf(stderr, "В модели задано кабин: %d\n", model->cabin_count);
        } else if (model->cabins[requested - 1].registered) {
            fprintf(stderr, "Кабина %d уже обслуживается другим процессом\n", requested);
        } else {
            chosen = requested - 1;
        }
    } else {
        for (int index = 0; index < model->cabin_count; ++index) {
            if (!model->cabins[index].registered) {
                chosen = index;
                break;
            }
        }
        if (chosen < 0) {
            fprintf(stderr, "Все %d кабин уже обслуживаются\n", model->cabin_count);
        }
    }

    if (chosen >= 0) {
        model->cabins[chosen].registered = true;
        model->cabins[chosen].pid = getpid();
        ++model->registered_cabins;
    }

    sync_post(&link->sync, SEM_MODEL);
    return chosen;
}

int main(int argc, char **argv)
{
    int requested = 0;
    if (argc == 2) {
        requested = atoi(argv[1]);
        if (requested <= 0) {
            fprintf(stderr, "Использование: %s [номер кабины]\n", argv[0]);
            return EXIT_FAILURE;
        }
    } else if (argc > 2) {
        fprintf(stderr, "Использование: %s [номер кабины]\n", argv[0]);
        return EXIT_FAILURE;
    }

    install_signal_handlers();

    StandaloneLink link;
    if (standalone_attach(&link) != SUCCESS) {
        return EXIT_FAILURE;
    }

    int cabin_index = register_cabin(&link, requested);
    if (cabin_index < 0 || standalone_announce(&link) != SUCCESS) {
        standalone_detach(&link);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "Кабина %d подключена (pid %ld), ожидание запуска модели\n",
            cabin_index + 1, (long)getpid());

    Status result = standalone_wait_start(&link);
    if (result == SUCCESS) {
        fprintf(stderr, "Кабина %d вышла на линию\n", cabin_index + 1);
        result = worker_loop(link.model, &link.sync, WORKER_CABIN, cabin_index);
        fprintf(stderr, "Кабина %d завершила работу\n", cabin_index + 1);
    }

    standalone_detach(&link);
    return result == SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
