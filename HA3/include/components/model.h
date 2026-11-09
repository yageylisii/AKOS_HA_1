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
    SERVICE_SETUP = 0,    // участники ещё подключаются
    SERVICE_RUNNING,      // движение разрешено
    SERVICE_STOPPED,      // остановка из-за ветра или неисправности
    SERVICE_EVACUATING,   // выполняется аварийная эвакуация
    SERVICE_FINISHED      // моделирование завершено
} ServiceStatus;

// набор семафоров одинаков для всех трёх вариантов программы
// отличается только способ их создания: sem_init или sem_open
//
// у каждого участника свой семафор разрешения такта. Общий счётный
// семафор здесь не годится: быстрый процесс успел бы забрать два
// разрешения подряд, пока соседний не получил ни одного, и такты
// участников разошлись бы. Подтверждение такта, наоборот, удобно
// собирать одним счётчиком: ведущему важно только их количество
enum {
    SEM_MODEL = 0,    // взаимное исключение при доступе к модели, начальное значение 1
    SEM_TICK_DONE,    // участник закончил такт, начальное значение 0
    SEM_ADMIN,        // управляющая программа разрешила моделирование
    SEM_REGISTERED,   // очередной участник подключился к модели
    SEM_START_BASE,   // начало блока персональных семафоров разрешения такта
    SEM_SLOT_COUNT = SEM_START_BASE + 1 + STATION_COUNT + MAX_CABINS
};

// предельное время ожидания ответа участника. Если процесс снят извне,
// ведущий не зависает на барьере навсегда, а завершает моделирование
enum { BARRIER_TIMEOUT_SECONDS = 30, REGISTRATION_TIMEOUT_SECONDS = 180 };

// вся модель целиком лежит в одном сегменте POSIX-разделяемой памяти
// указателей внутри нет: процессы отображают сегмент по разным адресам,
// поэтому связи выражены индексами массивов
typedef struct SharedModel {
    // используются только в режиме неименованных семафоров (pshared = 1)
    sem_t semaphores[SEM_SLOT_COUNT];

    Settings settings;
    Station stations[STATION_COUNT];
    Cabin cabins[MAX_CABINS];
    Passenger passengers[MAX_PASSENGERS];
    Statistics statistics;
    EventLog log;

    int current_time;        // секунды с начала работы в 09:00
    int cabin_count;         // копия settings.cabines для удобства участников
    int passengers_created;  // сколько пассажиров уже появилось
    int next_passenger_id;   // следующий свободный номер пассажира

    int status;              // тип ServiceStatus
    bool admission_closed;   // приём новых пассажиров закрыт
    bool wind_stop;          // остановка из-за сильного ветра
    bool technical_stop;     // остановка из-за технической неисправности
    int technical_stop_remaining; // осталось секунд до устранения неисправности

    bool shutdown;           // участникам пора завершать работу
    bool interrupted;        // поступил сигнал прерывания
    bool invariant_failed;   // обнаружено нарушение инварианта
    char invariant_message[192];

    // регистрация участников нужна варианту с независимыми программами
    int registered_cabins;
    int registered_stations;
    int registered_operators;
    int expected_workers;    // кабины и станции: сколько участников ждут такта
    bool start_allowed;      // управляющая программа разрешила моделирование

    unsigned int operator_rng; // отдельный поток случайных чисел оператора
    pid_t operator_pid;
} SharedModel;

// детерминированный генератор: у каждого участника свой поток чисел,
// поэтому одно и то же начальное значение даёт повторяемый сценарий
unsigned int model_next_random(unsigned int *state);
double model_random_unit(unsigned int *state);

// заполняет модель по настройкам. Вызывается ровно один раз создателем сегмента
Status model_init(SharedModel *model, const Settings *settings);

// помощники, доступные всем ролям. Вызываются при захваченном SEM_MODEL
bool model_is_stopped(const SharedModel *model);
bool model_movement_allowed(const SharedModel *model);
int model_waiting_passengers(const SharedModel *model, int station_index);
int model_passengers_in_system(const SharedModel *model);
bool model_all_cabins_parked(const SharedModel *model);
bool model_has_active_operations(const SharedModel *model);
bool model_is_complete(const SharedModel *model);
bool model_in_wind_interval(const SharedModel *model, int time);

// аварийная эвакуация: все пассажиры снимаются с маршрута, кабины очищаются
void model_evacuate(SharedModel *model);

// проверка инвариантов задания. Возвращает false и заполняет сообщение,
// если найдено нарушение
bool model_check_invariants(SharedModel *model);

// итоговая статистика в текстовом виде
Status model_report(const SharedModel *model, int out_fd, int file_fd);

#endif
