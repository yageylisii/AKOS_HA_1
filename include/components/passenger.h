#ifndef PASSENGER_H
#define PASSENGER_H

enum { PASSENGER_NAME_SIZE = 64 };
// генерация пассажиров происходит рандомно а id инкрементируется
typedef struct Passenger {
    int id; // Уникальный номер пассажира
    int priority; // 0 — VIP, 1 — обычный билет
} Passenger;

#endif
