#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>

#include "components/model.h"
#include "components/status.h"
#include "ipc/sync.h"

// ведущий процесс: продвигает модельное время, собирает такты участников
// в барьер и отвечает за вывод журнала
typedef struct ClockContext {
    SharedModel *model;
    SyncSet *sync;
    int out_fd;       // куда выводить журнал, обычно STDOUT_FILENO
    int file_fd;      // копия журнала в файле или -1
    int worker_count; // кабины и станции; оператор работает в отдельной фазе
} ClockContext;

// один такт: фаза оператора, затем фаза кабин и станций, затем вывод группы
Status clock_tick(ClockContext *context);

// полный прогон до завершения модели, прерывания или исчерпания времени
Status clock_run(ClockContext *context);

// сообщает участникам о завершении и выпускает их из барьера
Status clock_shutdown(ClockContext *context);

// обработчик прерывания: взводит флаг, общий для всех программ варианта
void install_signal_handlers(void);
bool signal_received(void);

#endif
