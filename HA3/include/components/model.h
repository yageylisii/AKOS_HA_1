#ifndef MODEL_H
#define MODEL_H

#include <semaphore.h>
#include <stdbool.h>
#include <sys/types.h>

#include "components/cabin.h"
#include "components/passenger.h"
#include "components/settings.h"
#include "components/station.h"
#include "components/statistics.h"
#include "components/status.h"
#include "inputs/input_logs.h"

enum { STATION_COUNT = 2, SERVICE_START_HOUR = 9 };

typedef enum ServiceStatus {
    SERVICE_SETUP = 0,    // Участники ещё подключаются
    SERVICE_RUNNING,      // Движение разрешено
    SERVICE_STOPPED,      // Остановка из-за ветра или неисправности
    SERVICE_EVACUATING,   // Выполняется аварийная эвакуация
    SERVICE_FINISHED      // Моделирование завершено
} ServiceStatus;

// Набор семафоров одинаков для всех трёх вариантов программы.
// Отличается только способ их создания: sem_init или sem_open.
//
// У каждого участника свой семафор разрешения такта. Общий счётный
// семафор здесь не годится: быстрый процесс успел бы забрать два
// разрешения подряд, пока соседний не получил ни одного, и такты
// участников разошлись бы. Подтверждение такта, наоборот, удобно
// собирать одним счётчиком: ведущему важно только их количество.
enum {
    SEM_MODEL = 0,    // Взаимное исключение при доступе к модели, начальное значение 1
    SEM_TICK_DONE,    // Участник закончил такт, начальное значение 0
    SEM_ADMIN,        // Управляющая программа разрешила моделирование
    SEM_REGISTERED,   // Очередной участник подключился к модели
    SEM_START_BASE,   // Начало блока персональных семафоров разрешения такта
    SEM_SLOT_COUNT = SEM_START_BASE + 1 + STATION_COUNT + MAX_CABINS
};

// Предельное время ожидания ответа участника. Если процесс снят извне,
// ведущий не зависает на барьере навсегда, а завершает моделирование.
enum { BARRIER_TIMEOUT_SECONDS = 30, REGISTRATION_TIMEOUT_SECONDS = 180 };

// Вся модель целиком лежит в одном сегменте POSIX-разделяемой памяти.
// Указателей внутри нет: процессы отображают сегмент по разным адресам,
// поэтому связи выражены индексами массивов.
typedef struct SharedModel {
    // Используются только в режиме неименованных семафоров (pshared = 1).
    sem_t semaphores[SEM_SLOT_COUNT];

    Settings settings;
    Station stations[STATION_COUNT];
    Cabin cabins[MAX_CABINS];
    Passenger passengers[MAX_PASSENGERS];
    Statistics statistics;
    EventLog log;

    int current_time;        // Секунды с начала работы в 09:00
    int cabin_count;         // Копия settings.cabines для удобства участников
    int passengers_created;  // Сколько пассажиров уже появилось
    int next_passenger_id;   // Следующий свободный номер пассажира

    int status;              // ServiceStatus
    bool admission_closed;   // Приём новых пассажиров закрыт
    bool wind_stop;          // Остановка из-за сильного ветра
    bool technical_stop;     // Остановка из-за технической неисправности
    int technical_stop_remaining; // Осталось секунд до устранения неисправности

    bool shutdown;           // Участникам пора завершать работу
    bool interrupted;        // Поступил сигнал прерывания
    bool invariant_failed;   // Обнаружено нарушение инварианта
    char invariant_message[192];

    // Регистрация участников нужна варианту с независимыми программами.
    int registered_cabins;
    int registered_stations;
    int registered_operators;
    int expected_workers;    // Кабины и станции: сколько участников ждут такта
    bool start_allowed;      // Управляющая программа разрешила моделирование

    unsigned int operator_rng; // Отдельный поток случайных чисел оператора
    pid_t operator_pid;
} SharedModel;

// Детерминированный генератор: у каждого участника свой поток чисел,
// поэтому одно и то же начальное значение даёт повторяемый сценарий.
unsigned int model_next_random(unsigned int *state);
double model_random_unit(unsigned int *state);

// Заполняет модель по настройкам. Вызывается ровно один раз создателем сегмента.
Status model_init(SharedModel *model, const Settings *settings);

// Помощники, доступные всем ролям. Вызываются при захваченном SEM_MODEL.
bool model_is_stopped(const SharedModel *model);
bool model_movement_allowed(const SharedModel *model);
int model_waiting_passengers(const SharedModel *model, int station_index);
int model_passengers_in_system(const SharedModel *model);
bool model_all_cabins_parked(const SharedModel *model);
bool model_has_active_operations(const SharedModel *model);
bool model_is_complete(const SharedModel *model);
bool model_in_wind_interval(const SharedModel *model, int time);

// Аварийная эвакуация: все пассажиры снимаются с маршрута, кабины очищаются.
void model_evacuate(SharedModel *model);

// Проверка инвариантов задания. Возвращает false и заполняет сообщение,
// если найдено нарушение.
bool model_check_invariants(SharedModel *model);

// Итоговая статистика в текстовом виде.
Status model_report(const SharedModel *model, int out_fd, int file_fd);

#endif
