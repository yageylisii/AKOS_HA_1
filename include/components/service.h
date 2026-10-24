// текущее положение системы
#ifndef SERVICE_H
#define SERVICE_H

#include <stdbool.h>
#include "components/cabin.h"
#include "components/settings.h"
#include "components/station.h"
#include "components/statistics.h"
#include "inputs/input_logs.h"

// Движение разрешено только при SERVICE_RUNNING и отсутствии обеих причин остановки.
// При current_time >= settings.work_time приём новых пассажиров закрывается.
// Модельное время продолжается до доставки пассажиров и завершения начатых рейсов или эвакуации.
// При остановке состояние кабин сохраняется, operation_time_remaining не уменьшается.
// При завершении освобождаются пассажиры кабин, массив кабин и обе очереди кабин каждой станции.

enum { STATION_COUNT = 2, SERVICE_START_HOUR = 9 };

typedef enum ServiceStatus {
    SERVICE_RUNNING = 0,
    SERVICE_STOPPED,
    SERVICE_EVACUATING,
    SERVICE_FINISHED
} ServiceStatus;

typedef struct Service {
    EventLog event_log; // Буфер событий одного модельного времени
    Settings settings;
    Station stations[STATION_COUNT]; // Станции А и Б
    Cabin *cabins; // Массив из settings.cabines кабин; выделяется при запуске
    Statistics statistics;

    int current_time; // Секунды с начала работы в 09:00; начальное значение — 0
    bool admission_closed; // После закрытия входа доставляем уже принятых пассажиров
    ServiceStatus status;

    bool wind_stop; // Остановка из-за сильного ветра
    bool technical_stop; // Остановка из-за неисправности
    int technical_stop_remaining; // Осталось секунд до устранения неисправности
    int next_passenger_id; // Следующий свободный ID 
} Service;

// Перед init: Service service = {0}; настройки уже заполнены.
// ID станций — 1 и 2, ID кабин — от 1 до settings.cabines.
// Настройки после init не изменять; повторный init — только после destroy.
Status service_init(Service *service);
Status service_step(Service *service); // Один шаг модельного времени (секунда).
Status service_run(Service *service);
Status service_evacuate(Service *service);
void service_destroy(Service *service); // Освобождение памяти, статистика сохраняется.

#endif
