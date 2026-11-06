#define _POSIX_C_SOURCE 200809L

#include "roles/launcher.h"
#include "asserts.h"
#include "components/model.h"
#include "inputs/input_data.h"
#include "ipc/shared_memory.h"
#include "roles/clock.h"
#include "roles/roles.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// единый родительский процесс запускает требуемое число дочерних:
// одного оператора, по одному процессу на семейство пассажиров каждой
// станции и по одному процессу на кабину. Обмен данными идёт через
// разделяемую память POSIX, согласование — через семафоры POSIX

typedef struct ChildTable {
    pid_t items[1 + STATION_COUNT + MAX_CABINS];
    int count;
} ChildTable;

// код дочернего процесса. Отображение памяти и семафоры унаследованы
// при fork, поэтому их не нужно открывать заново
static void run_child(SharedModel *model, SyncSet *sync, SharedMemory *memory,
                      int kind, int index)
{
    // потомок не владеет ресурсами: удалять их обязан родитель
    sync->owner = false;
    memory->owner = false;

    Status result = worker_loop(model, sync, kind, index);

    sync_close(sync);
    shared_memory_detach(memory);
    _exit(result == SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE);
}

static Status spawn_child(ChildTable *children, SharedModel *model, SyncSet *sync,
                          SharedMemory *memory, int kind, int index)
{
    pid_t child = fork();
    if (child == -1) {
        perror("fork");
        return ERROR;
    }
    if (child == 0) {
        run_child(model, sync, memory, kind, index);
    }
    children->items[children->count++] = child;

    if (kind == WORKER_OPERATOR) {
        model->operator_pid = child;
        model->registered_operators = 1;
    } else if (kind == WORKER_STATION) {
        model->stations[index].pid = child;
        model->stations[index].registered = true;
    } else {
        model->cabins[index].pid = child;
        model->cabins[index].registered = true;
    }
    return SUCCESS;
}

// снимаем всех потомков, если запуск не удался на середине
static void terminate_children(ChildTable *children)
{
    for (int index = 0; index < children->count; ++index) {
        kill(children->items[index], SIGTERM);
    }
    for (int index = 0; index < children->count; ++index) {
        while (waitpid(children->items[index], NULL, 0) == -1 && errno == EINTR) {
            continue;
        }
    }
    children->count = 0;
}

static int wait_children(ChildTable *children)
{
    int failures = 0;
    for (int index = 0; index < children->count; ++index) {
        int status = 0;
        while (waitpid(children->items[index], &status, 0) == -1) {
            if (errno != EINTR) {
                perror("waitpid");
                ++failures;
                break;
            }
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
            ++failures;
        }
    }
    return failures;
}

static Status read_settings(const RunOptions *options, Settings *settings)
{
    if (options->config_path != NULL) {
        return input_data_from_file(options->config_path, settings);
    }
    return input_data(settings);
}

Status run_forked_application(int argc, char **argv, int mode,
                              const char *shm_name, const char *sem_prefix)
{
    SOFT_ASSERT(shm_name != NULL && sem_prefix != NULL, "Не заданы имена ресурсов", ERROR);

    RunOptions options;
    if (cli_parse(argc, argv, &options) != SUCCESS) {
        return ERROR;
    }
    if (options.show_help) {
        cli_usage(argv[0]);
        return SUCCESS;
    }

    Settings settings = {0};
    if (read_settings(&options, &settings) != SUCCESS) {
        return ERROR;
    }
    if (settings_apply_options(&settings, &options) != SUCCESS) {
        return ERROR;
    }

    // обработчики сигналов ставим до fork, чтобы они достались потомкам
    install_signal_handlers();

    SharedMemory memory;
    if (shared_memory_create(&memory, shm_name, sizeof(SharedModel)) != SUCCESS) {
        return ERROR;
    }
    SharedModel *model = (SharedModel *)memory.address;

    if (model_init(model, &settings) != SUCCESS) {
        shared_memory_destroy(&memory);
        return ERROR;
    }

    SyncSet sync;
    Status prepared = (mode == SYNC_UNNAMED) ? sync_create_unnamed(&sync, model)
                                             : sync_create_named(&sync, sem_prefix);
    if (prepared != SUCCESS) {
        shared_memory_destroy(&memory);
        return ERROR;
    }

    int file_fd = open(options.log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd == -1) {
        perror(options.log_path);
    }

    model->status = SERVICE_RUNNING;
    model->start_allowed = true;

    ChildTable children = {.count = 0};
    Status launch = spawn_child(&children, model, &sync, &memory, WORKER_OPERATOR, 0);
    for (int index = 0; launch == SUCCESS && index < STATION_COUNT; ++index) {
        launch = spawn_child(&children, model, &sync, &memory, WORKER_STATION, index);
    }
    for (int index = 0; launch == SUCCESS && index < model->cabin_count; ++index) {
        launch = spawn_child(&children, model, &sync, &memory, WORKER_CABIN, index);
    }

    if (launch != SUCCESS) {
        terminate_children(&children);
        if (file_fd >= 0) {
            close(file_fd);
        }
        sync_destroy(&sync);
        shared_memory_destroy(&memory);
        return ERROR;
    }

    fprintf(stderr, "Запущено процессов: оператор 1, станции %d, кабины %d\n",
            STATION_COUNT, model->cabin_count);

    ClockContext context = {
        .model = model,
        .sync = &sync,
        .out_fd = STDOUT_FILENO,
        .file_fd = file_fd,
        .worker_count = STATION_COUNT + model->cabin_count
    };

    Status result = clock_run(&context);

    int failures = wait_children(&children);
    if (failures > 0) {
        fprintf(stderr, "Процессов завершилось с ошибкой: %d\n", failures);
    }

    model_report(model, STDOUT_FILENO, file_fd);

    if (file_fd >= 0) {
        close(file_fd);
    }
    // удаление семафоров и разделяемой памяти при любом способе завершения
    sync_destroy(&sync);
    shared_memory_destroy(&memory);

    return result;
}
