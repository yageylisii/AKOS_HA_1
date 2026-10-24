// текущее положение системы
#ifndef SERVICE_H
#define SERVICE_H

#include <stdbool.h>
#include <signal.h>
#include "components/cabin.h"
#include "components/settings.h"
#include "components/station.h"
#include "components/statistics.h"
#include "inputs/input_logs.h"

struct ThreadRuntime;

enum { STATION_COUNT = 2, SERVICE_START_HOUR = 9 };

typedef enum ServiceStatus {
    SERVICE_RUNNING = 0,
    SERVICE_STOPPED,
    SERVICE_EVACUATING,
    SERVICE_FINISHED
} ServiceStatus;

extern volatile sig_atomic_t service_interrupted;

typedef struct Service {
    EventLog event_log; // Буфер событий одного модельного времени
    Settings settings;
    Station stations[STATION_COUNT]; // Станции А и Б
    Cabin *cabins; // Массив из settings.cabin_count кабин; выделяется при запуске
    Statistics statistics;

    int current_time; // Секунды с начала работы в 09:00; начальное значение — 0
    bool admission_closed; // После закрытия входа доставляем уже принятых пассажиров
    ServiceStatus status;

    bool wind_stop; // Остановка из-за сильного ветра
    bool technical_stop; // Остановка из-за неисправности
    int repair_time_left; // Осталось секунд до устранения неисправности
    int next_passenger_id; // Следующий свободный ID
    struct ThreadRuntime *runtime; // Потоки сущностей; NULL в пошаговых тестах
} Service;

// перед инициализацией обнулить Service и заполнить настройки
// после инициализации не менять настройки до service_destroy

// создаёт кабины и размещает их на станциях
Status service_init(Service *service);
// выполняет один шаг модельного времени
Status service_step(Service *service);
// запускает потоки и моделирование до завершения или прерывания
Status service_run(Service *service);
// эвакуирует всех оставшихся пассажиров и завершает модель
Status service_evacuate(Service *service);
// завершает потоки и освобождает память модели
void service_destroy(Service *service);

#endif
