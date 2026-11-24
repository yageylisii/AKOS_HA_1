#ifndef ROLES_H
#define ROLES_H

#include <stdbool.h>

#include "components/model.h"
#include "components/status.h"
#include "ipc/sync.h"

// оператор, кабины и семейства пассажиров — самостоятельные участники модели
// каждый из них в своём процессе выполняет один шаг модельного времени за такт
// шаги вызываются при захваченном семафоре SEM_MODEL

// оператор: погода, технические неисправности, закрытие входа,
// разрешение отправления кабин с закрытыми дверями
void operator_step(SharedModel *model);

// кабина: высадка, посадка, закрытие дверей, движение, допуск на станцию
void cabin_step(SharedModel *model, int cabin_index);

// семейство пассажиров станции: появление новых пассажиров по интервалу
void station_step(SharedModel *model, int station_index);

// общий цикл участника: ждёт такт, выполняет шаг, отчитывается о завершении
// kind задаёт, какую пару семафоров использовать
typedef enum WorkerKind {
    WORKER_OPERATOR = 0,
    WORKER_CABIN,
    WORKER_STATION
} WorkerKind;

// index игнорируется для оператора
Status worker_loop(SharedModel *model, SyncSet *sync, int kind, int index);

// номер персонального семафора разрешения такта для участника
// ведущий процесс открывает такт, обращаясь к тем же номерам
int worker_start_slot(int kind, int index);

#endif
