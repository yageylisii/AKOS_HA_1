#define _POSIX_C_SOURCE 200809L

#include "roles/clock.h"
#include "asserts.h"
#include "components/queue.h"
#include "roles/roles.h"

#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Ведущий процесс продвигает модельное время. Он не моделирует
// предметные сущности, а только открывает такт участникам,
// дожидается их подтверждений и выводит группу событий.

static volatile sig_atomic_t g_signal_received = 0;

static void handle_signal(int number)
{
    (void)number;
    g_signal_received = 1;
}

void install_signal_handlers(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    // Без SA_RESTART: прерванный sem_wait вернёт EINTR и мы заметим сигнал.
    action.sa_flags = 0;

    // SIGINT — Ctrl+C, SIGQUIT — Ctrl+\, SIGTERM — завершение извне.
    // SIGPIPE добавлен, чтобы обрыв вывода (например, при `| head`)
    // тоже приводил к штатной очистке семафоров и разделяемой памяти,
    // а не к мгновенной гибели процесса от действия по умолчанию.
    static const int numbers[] = {SIGINT, SIGQUIT, SIGTERM, SIGPIPE};
    for (size_t index = 0; index < sizeof(numbers) / sizeof(numbers[0]); ++index) {
        if (sigaction(numbers[index], &action, NULL) == -1) {
            perror("sigaction");
        }
    }
}

bool signal_received(void)
{
    return g_signal_received != 0;
}

// Сбор подтверждений такта. Ожидание ограничено по времени: если процесс
// участника сняли извне, ведущий не должен зависнуть на барьере навсегда.
static Status collect_confirmations(ClockContext *context, int count)
{
    for (int index = 0; index < count; ++index) {
        bool timed_out = false;
        TRY(sync_timedwait(context->sync, SEM_TICK_DONE, BARRIER_TIMEOUT_SECONDS, &timed_out));
        if (timed_out) {
            fprintf(stderr,
                    "Участник не ответил за %d с: подтверждений %d из %d\n",
                    BARRIER_TIMEOUT_SECONDS, index, count);
            return ERROR;
        }
    }
    return SUCCESS;
}

// Фаза оператора: он решает вопросы остановки и разрешает отправление
// до того, как кабины начнут свой шаг в этом же такте.
static Status run_operator_phase(ClockContext *context)
{
    TRY(sync_post(context->sync, worker_start_slot(WORKER_OPERATOR, 0)));
    return collect_confirmations(context, 1);
}

// Фаза кабин и станций. Каждому участнику открывается его персональный
// семафор, после чего ведущий собирает ровно worker_count подтверждений.
static Status run_worker_phase(ClockContext *context)
{
    for (int index = 0; index < STATION_COUNT; ++index) {
        TRY(sync_post(context->sync, worker_start_slot(WORKER_STATION, index)));
    }
    for (int index = 0; index < context->model->cabin_count; ++index) {
        TRY(sync_post(context->sync, worker_start_slot(WORKER_CABIN, index)));
    }
    return collect_confirmations(context, context->worker_count);
}

Status clock_tick(ClockContext *context)
{
    SOFT_ASSERT(context != NULL && context->model != NULL && context->sync != NULL,
                "Не передан контекст ведущего процесса", ERROR);

    TRY(run_operator_phase(context));
    TRY(run_worker_phase(context));

    SharedModel *model = context->model;

    // Между тактами все участники стоят на барьере, поэтому состояние
    // модели согласовано и его можно проверить и вывести.
    TRY(sync_wait(context->sync, SEM_MODEL));

    if (!model_check_invariants(model)) {
        log_event(&model->log, model->current_time,
                  "🛑 Нарушен инвариант модели: %s", model->invariant_message);
    }

    // Решение о завершении принимается до вывода, чтобы все события
    // одной модельной секунды попали в одну группу журнала.
    bool interrupted = model->interrupted || signal_received();
    if (interrupted && model->status != SERVICE_FINISHED) {
        model->interrupted = true;
        log_event(&model->log, model->current_time,
                  "🛑 Получен сигнал прерывания, выполняется аварийная эвакуация");
        model_evacuate(model);
    } else if (model->invariant_failed) {
        model_evacuate(model);
    } else if (model_is_complete(model)) {
        log_event(&model->log, model->current_time,
                  "💵 Все принятые пассажиры доставлены, кабины на стоянке");
        model->status = SERVICE_FINISHED;
    } else if (model->current_time > model->settings.work_time + MAX_DRAIN_TIME) {
        // Защита от бесконечного ожидания при необычном сочетании остановок.
        log_event(&model->log, model->current_time,
                  "🛑 Превышено предельное время завершения работы");
        model_evacuate(model);
    }

    log_flush(&model->log, context->out_fd, context->file_fd,
              model->settings.display_delay_ms);

    if (model->status != SERVICE_FINISHED) {
        ++model->current_time;
    }

    return sync_post(context->sync, SEM_MODEL);
}

Status clock_shutdown(ClockContext *context)
{
    SOFT_ASSERT(context != NULL && context->model != NULL && context->sync != NULL,
                "Не передан контекст ведущего процесса", ERROR);

    TRY(sync_wait(context->sync, SEM_MODEL));
    context->model->shutdown = true;
    TRY(sync_post(context->sync, SEM_MODEL));

    // Последний такт выпускает всех участников из барьера: они увидят
    // флаг завершения, подтвердят такт и выйдут из своих циклов.
    TRY(run_operator_phase(context));
    return run_worker_phase(context);
}

Status clock_run(ClockContext *context)
{
    SOFT_ASSERT(context != NULL && context->model != NULL, "Не передан контекст", ERROR);

    // Первое событие попадает в ту же группу, что и прибытия первой минуты.
    TRY(sync_wait(context->sync, SEM_MODEL));
    log_event(&context->model->log, context->model->current_time,
              "🧢 Начало работы канатной дороги");
    TRY(sync_post(context->sync, SEM_MODEL));

    // Сбой барьера означает, что участник не отвечает. Моделирование
    // в этом случае прекращаем, но ресурсы всё равно освобождаем.
    while (context->model->status != SERVICE_FINISHED) {
        if (clock_tick(context) != SUCCESS) {
            context->model->shutdown = true;
            return ERROR;
        }
    }

    return clock_shutdown(context);
}
