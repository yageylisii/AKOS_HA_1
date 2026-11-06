// Вариант на оценку 7-8 баллов: управляющая программа.
//
// Приложение состоит из отдельных программ-процессов, запускаемых
// независимо друг от друга. Эта программа запускается первой:
// она создаёт разделяемую память POSIX и именованные POSIX-семафоры,
// принимает настройки, дожидается подключения остальных участников,
// ведёт модельное время и выводит журнал. Она же удаляет все
// ресурсы межпроцессного взаимодействия при завершении любым способом.

#define _POSIX_C_SOURCE 200809L

#include "asserts.h"
#include "components/model.h"
#include "inputs/input_data.h"
#include "ipc/ipc_names.h"
#include "ipc/shared_memory.h"
#include "ipc/sync.h"
#include "roles/clock.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static void sleep_ms(int milliseconds)
{
    struct timespec remaining = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (long)(milliseconds % 1000) * 1000000L
    };
    nanosleep(&remaining, &remaining);
}

// Ждём, пока независимо запущенные программы займут свои места.
// Каждый подключившийся участник поднимает счётный семафор регистрации,
// поэтому опрашивать состояние модели в цикле не нужно: достаточно
// снять с семафора ровно столько подтверждений, сколько ждём участников.
// Ожидание ограничено по времени, иначе управляющая программа висела бы
// вечно, если кого-то так и не запустили.
static Status wait_for_participants(SharedModel *model, SyncSet *sync)
{
    int expected = 1 + STATION_COUNT + model->cabin_count;

    for (int joined = 0; joined < expected; ++joined) {
        bool timed_out = false;
        TRY(sync_timedwait(sync, SEM_REGISTERED, REGISTRATION_TIMEOUT_SECONDS, &timed_out));

        if (timed_out) {
            fprintf(stderr, "Подключились не все участники: %d из %d\n", joined, expected);
            return ERROR;
        }
        if (signal_received()) {
            fprintf(stderr, "Ожидание участников прервано пользователем\n");
            return ERROR;
        }
        fprintf(stderr, "Подключено участников: %d из %d\n", joined + 1, expected);
    }
    return SUCCESS;
}

static Status read_settings(const RunOptions *options, Settings *settings)
{
    if (options->config_path != NULL) {
        return input_data_from_file(options->config_path, settings);
    }
    return input_data(settings);
}

int main(int argc, char **argv)
{
    RunOptions options;
    if (cli_parse(argc, argv, &options) != SUCCESS) {
        return EXIT_FAILURE;
    }
    if (options.show_help) {
        cli_usage(argv[0]);
        return EXIT_SUCCESS;
    }

    Settings settings = {0};
    if (read_settings(&options, &settings) != SUCCESS) {
        return EXIT_FAILURE;
    }
    if (settings_apply_options(&settings, &options) != SUCCESS) {
        return EXIT_FAILURE;
    }

    install_signal_handlers();

    SharedMemory memory;
    if (shared_memory_create(&memory, SHM_NAME_STANDALONE, sizeof(SharedModel)) != SUCCESS) {
        return EXIT_FAILURE;
    }
    SharedModel *model = (SharedModel *)memory.address;

    if (model_init(model, &settings) != SUCCESS) {
        shared_memory_destroy(&memory);
        return EXIT_FAILURE;
    }

    SyncSet sync;
    if (sync_create_named(&sync, SEM_PREFIX_STANDALONE) != SUCCESS) {
        shared_memory_destroy(&memory);
        return EXIT_FAILURE;
    }

    fprintf(stderr,
            "Ресурсы созданы. Запустите в других терминалах:\n"
            "  ./cc_operator\n"
            "  ./cc_station A   и   ./cc_station B\n"
            "  ./cc_cabin   (%d раз)\n",
            model->cabin_count);

    Status result = wait_for_participants(model, &sync);

    int file_fd = -1;
    if (result == SUCCESS) {
        file_fd = open(options.log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (file_fd == -1) {
            perror(options.log_path);
        }

        if (sync_wait(&sync, SEM_MODEL) == SUCCESS) {
            model->status = SERVICE_RUNNING;
            model->start_allowed = true;
            sync_post(&sync, SEM_MODEL);
        }
        // Поднимаем администратора: первый ожидающий участник пройдёт
        // и сам пропустит следующего, и так по цепочке все разом.
        sync_post(&sync, SEM_ADMIN);

        ClockContext context = {
            .model = model,
            .sync = &sync,
            .out_fd = STDOUT_FILENO,
            .file_fd = file_fd,
            .worker_count = STATION_COUNT + model->cabin_count
        };

        result = clock_run(&context);
        model_report(model, STDOUT_FILENO, file_fd);
    } else {
        // Участники могли уже подключиться: отпускаем их из ожидания.
        if (sync_wait(&sync, SEM_MODEL) == SUCCESS) {
            model->shutdown = true;
            sync_post(&sync, SEM_MODEL);
        }
        sync_post(&sync, SEM_ADMIN);
        sleep_ms(500);
    }

    if (file_fd >= 0) {
        close(file_fd);
    }

    // Управляющая программа создала ресурсы, она же их и удаляет.
    sync_destroy(&sync);
    shared_memory_destroy(&memory);

    return result == SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
