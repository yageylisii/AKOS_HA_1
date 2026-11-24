#ifndef CABIN_H
#define CABIN_H

#include <stdbool.h>
#include <sys/types.h>

#include "components/settings.h"

// состояния кабины повторяют модель HA1
typedef enum Statement {
    ON_STATION = 0,         // на станции, двери закрыты, ждёт разрешения оператора
    IN_WALK = 1,            // в пути между станциями
    BOARDING = 2,           // посадка, двери открыты
    DROP = 3,               // высадка, двери открыты
    WAITING_FOR_STATION = 4,// перед станцией назначения, двери закрыты
    PARKED = 5              // резерв или стоянка вне мест обслуживания
} Statement;

typedef struct Cabin {
    int id;                              // номер кабины, начиная с 1
    int state;                           // тип Statement
    int passengers;                      // сколько человек внутри, не больше capacity
    int passenger_ids[MAX_CABIN_CAPACITY]; // номера пассажиров внутри кабины
    int boarding_passenger;              // пассажир, закреплённый в начале посадки, или -1
    int operation_time_remaining;        // осталось секунд текущей операции
    bool doors_open;                     // инвариант: отправление только при false
    bool departure_requested;            // кабина попросила разрешение на отправление
    bool departure_allowed;              // оператор разрешил отправление
    int station_index;                   // станция, у которой находится кабина (0 или 1)
    int destination_index;               // станция назначения при движении
    bool on_platform;                    // кабина занимает одно из мест станции
    pid_t pid;                           // процесс, обслуживающий кабину
    bool registered;                     // процесс кабины подключился к модели
} Cabin;

#endif
