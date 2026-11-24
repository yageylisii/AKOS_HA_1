#ifndef MODEL_H
#define MODEL_H

#include <stdbool.h>
#include <stddef.h>

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

/*
модель целиком хранится на сервере. В HA3 она лежала в разделяемой памяти,
теперь сервер передаёт её участнику по TCP вместе с разрешением такта,
а участник возвращает изменённую копию. Указателей внутри по-прежнему нет,
связи выражены индексами массивов. В сетевом формате поля записываются
явно, независимо от размера и выравнивания C-структур
*/
typedef struct Model {
    Settings settings;
    Station stations[STATION_COUNT];
    Cabin cabins[MAX_CABINS];
    Passenger passengers[MAX_PASSENGERS];
    Statistics statistics;

    int current_time;        // секунды с начала работы в 09:00
    int cabin_count;         // копия settings.cabines для удобства участников
    int passengers_created;  // сколько пассажиров уже появилось
    int next_passenger_id;   // следующий свободный номер пассажира

    int status;              // тип ServiceStatus
    bool admission_closed;   // приём новых пассажиров закрыт
    bool wind_stop;          // остановка из-за сильного ветра
    bool technical_stop;     // остановка из-за технической неисправности
    int technical_stop_remaining; // осталось секунд до устранения неисправности

    bool interrupted;        // поступил сигнал прерывания или команда остановки
    bool invariant_failed;   // обнаружено нарушение инварианта
    char invariant_message[192];

    unsigned int operator_rng; // отдельный поток случайных чисел оператора

    /*
    журнал обязательно последнее поле: по сети передаётся только занятая
    часть буфера, а не все LOG_BUFFER_SIZE байт
    */
    EventLog log;
} Model;

enum { MODEL_WIRE_CAPACITY = 98304 };

Status model_to_wire(const Model *model, void *data, size_t capacity, size_t *length);
// Разбор байтов; внешние данные следует принимать через model_from_wire с валидацией
Status model_decode_wire(Model *model, const void *data, size_t length);

// сколько байт модели нужно передать, чтобы доставить её вместе с журналом
size_t model_wire_size(const Model *model);

// размер модели без текста журнала: минимально допустимое сообщение
size_t model_wire_header_size(void);

/*
принимает модель из сети: проверяет длину и все индексы, которые позже
используются для доступа к массивам. Чужие байты без проверки могли бы
вывести очередь за границы буфера
*/
Status model_from_wire(Model *model, const void *data, size_t length);

// детерминированный генератор: у каждого участника свой поток чисел,
// поэтому одно и то же начальное значение даёт повторяемый сценарий
unsigned int model_next_random(unsigned int *state);
double model_random_unit(unsigned int *state);

// заполняет модель по настройкам. Вызывается один раз сервером
Status model_init(Model *model, const Settings *settings);

// помощники, доступные всем ролям
bool model_is_stopped(const Model *model);
bool model_movement_allowed(const Model *model);
int model_waiting_passengers(const Model *model, int station_index);
int model_passengers_in_system(const Model *model);
bool model_all_cabins_parked(const Model *model);
bool model_has_active_operations(const Model *model);
bool model_is_complete(const Model *model);
bool model_in_wind_interval(const Model *model, int time);

// аварийная эвакуация: все пассажиры снимаются с маршрута, кабины очищаются
void model_evacuate(Model *model);

// проверка инвариантов задания. Возвращает false и заполняет сообщение,
// если найдено нарушение
bool model_check_invariants(Model *model);

// итоговая статистика в текстовом виде
Status model_report_text(const Model *model, char *buffer, size_t capacity);
Status model_report(const Model *model, int out_fd, int file_fd);

// сводная таблица состояния для клиента-наблюдателя: станции, кабины, счётчики
Status model_board_text(const Model *model, char *buffer, size_t capacity);

const char *model_status_name(const Model *model);
const char *cabin_state_name(int state);

#endif
