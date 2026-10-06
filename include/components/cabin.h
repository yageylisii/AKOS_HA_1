#ifndef CABIN_H
#define CABIN_H

#include <stdbool.h>
#include "components/passenger.h"


typedef enum Statement {
    ON_STATION = 0,
    IN_WALK = 1,
    BOARDING = 2,
    DROP = 3,
    WAITING_FOR_STATION = 4, // Перед станцией, двери закрыты
    PARKED = 5 // Резерв/стоянка вне мест обслуживания
} Statement;

typedef struct Cabin {
    int id; // Номер кабины
    Passenger boarding_passenger; // Закреплён в начале посадки
    bool has_boarding_passenger;
    int passengers;  // Текущее количество пассажиров (от 0 до capacity)
    Passenger *passenger_list; // Массив из capacity пассажиров; выделяется при создании кабины
    int operation_time_remaining; // Осталось секунд движения, посадки или высадки
    bool doors_open;  // 1 — двери открыты, 0 — закрыты
    Statement state; // состояние кабины
    int departure_station;   // Станция отправления
    int destination_station; // Станция назначения
} Cabin ;




#endif
