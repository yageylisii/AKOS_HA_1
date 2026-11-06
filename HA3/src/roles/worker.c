#define _POSIX_C_SOURCE 200809L

#include "roles/roles.h"
#include "asserts.h"
#include "roles/clock.h"

// Общий цикл участника модели. Все участники устроены одинаково:
// ждут разрешения такта на своём семафоре, выполняют одну секунду
// моделирования под взаимным исключением и подтверждают завершение такта.
// Такая схема гарантирует, что ведущий процесс увидит согласованное
// состояние модели между тактами.

// Блок персональных семафоров: сначала оператор, затем станции, затем кабины.
int worker_start_slot(int kind, int index)
{
    switch (kind) {
        case WORKER_OPERATOR:
            return SEM_START_BASE;
        case WORKER_STATION:
            return SEM_START_BASE + 1 + index;
        case WORKER_CABIN:
            return SEM_START_BASE + 1 + STATION_COUNT + index;
        default:
            return SEM_START_BASE;
    }
}

Status worker_loop(SharedModel *model, SyncSet *sync, int kind, int index)
{
    if (model == NULL || sync == NULL) {
        return ERROR;
    }

    // Оператор работает в отдельной фазе такта, до кабин и станций,
    // но подтверждение такта у всех участников общее.
    int start_slot = worker_start_slot(kind, index);

    for (;;) {
        TRY(sync_wait(sync, start_slot));
        TRY(sync_wait(sync, SEM_MODEL));

        bool finish = model->shutdown;
        if (!finish) {
            switch (kind) {
                case WORKER_OPERATOR:
                    operator_step(model);
                    break;
                case WORKER_CABIN:
                    cabin_step(model, index);
                    break;
                case WORKER_STATION:
                    station_step(model, index);
                    break;
                default:
                    break;
            }
        }

        // Сигнал прерывания превращается в общий флаг: участник не выходит
        // из барьера сам, иначе ведущий процесс заблокируется навсегда.
        if (signal_received() && !model->interrupted) {
            model->interrupted = true;
        }

        TRY(sync_post(sync, SEM_MODEL));
        TRY(sync_post(sync, SEM_TICK_DONE));

        if (finish) {
            break;
        }
    }
    return SUCCESS;
}
