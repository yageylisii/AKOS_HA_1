#ifndef CABIN_H
#define CABIN_H

#include <stdbool.h>
#include "components/passenger.h"


typedef enum CabinState {
    ON_STATION = 0, // на станции
    IN_TRANSIT = 1, // в пути
    BOARDING = 2, // посадка
    UNLOADING = 3, // высадка
    WAITING_FOR_STATION = 4, // Перед станцией, двери закрыты
    PARKED = 5 // Резерв/стоянка вне мест обслуживания
} CabinState;

typedef struct Cabin {
    int id; // Номер кабины
    Passenger boarding_passenger; // Закреплён в начале посадки
    bool has_boarding_passenger;
    int passengers;  // Текущее количество пассажиров (от 0 до capacity)
    Passenger *passenger_list; // Массив из capacity пассажиров; выделяется при создании кабины
    int time_left; // Осталось секунд движения, посадки или высадки
    bool doors_open;  // 1 — двери открыты, 0 — закрыты
    CabinState state; // состояние кабины
    int departure_station;   // Станция отправления
    int destination_station; // Станция назначения
} Cabin ;




#endif
