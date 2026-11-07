// программа оператора канатной дороги
// запускается независимо от остальных и подключается к разделяемой
// памяти и именованным семафорам, созданным программой cc_control

#define _POSIX_C_SOURCE 200809L

#include "roles/clock.h"
#include "roles/roles.h"
#include "roles/standalone.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// регистрация участника: модель узнаёт, что оператор подключился
static Status register_operator(StandaloneLink *link)
{
    if (sync_wait(&link->sync, SEM_MODEL) != SUCCESS) {
        return ERROR;
    }

    Status result = SUCCESS;
    if (link->model->registered_operators >= 1) {
        fprintf(stderr, "Оператор уже подключён к модели\n");
        result = ERROR;
    } else {
        link->model->registered_operators = 1;
        link->model->operator_pid = getpid();
    }

    sync_post(&link->sync, SEM_MODEL);
    return result;
}

int main(void)
{
    install_signal_handlers();

    StandaloneLink link;
    if (standalone_attach(&link) != SUCCESS) {
        return EXIT_FAILURE;
    }
    if (register_operator(&link) != SUCCESS || standalone_announce(&link) != SUCCESS) {
        standalone_detach(&link);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "Оператор подключён (pid %ld), ожидание запуска модели\n",
            (long)getpid());

    Status result = standalone_wait_start(&link);
    if (result == SUCCESS) {
        fprintf(stderr, "Оператор приступил к работе\n");
        result = worker_loop(link.model, &link.sync, WORKER_OPERATOR, 0);
        fprintf(stderr, "Оператор завершил работу\n");
    }

    standalone_detach(&link);
    return result == SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
