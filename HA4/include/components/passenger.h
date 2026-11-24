#ifndef PASSENGER_H
#define PASSENGER_H

// инвариант: пассажир находится либо на станции, либо ровно в одной кабине
// состояние вместе с полем cabin_index однозначно определяет его положение
typedef enum PassengerState {
    PASSENGER_NONE = 0,  // слот не занят: пассажир ещё не появился
    PASSENGER_WAITING,   // стоит в очереди станции origin
    PASSENGER_ENTERING,  // закреплён за кабиной, выполняется посадка
    PASSENGER_RIDING,    // находится в кабине cabin_index
    PASSENGER_DELIVERED, // доставлен на станцию назначения
    PASSENGER_EVACUATED  // снят с маршрута при аварийной эвакуации
} PassengerState;

enum { PRIORITY_VIP = 0, PRIORITY_REGULAR = 1 };

typedef struct Passenger {
    int id;            // уникальный номер, начиная с 1
    int priority;      // значения PRIORITY_VIP или PRIORITY_REGULAR
    int state;         // тип PassengerState
    int origin;        // индекс станции появления: 0 или 1
    int destination;   // индекс станции назначения: 0 или 1
    int cabin_index;   // индекс кабины или -1, если пассажир не в кабине
    int arrival_time;  // модельное время появления на станции
    int finish_time;   // модельное время доставки или эвакуации
} Passenger;

#endif
