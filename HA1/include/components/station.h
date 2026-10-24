#ifndef STATION_H
#define STATION_H

#include "queues/queue_passenger.h"
#include "queues/queue_cabin.h"

enum { STATION_CABIN_CAPACITY = 3 };

typedef struct Station {
    int ID_station; // 1 или 2

    Queue vip;     // Очередь VIP-пассажиров
    Queue regular; // Очередь обычных пассажиров

    CabinQueue waiting_cabins; // Ожидание вне станции, отдельно от трёх мест
    CabinQueue cabins; // Очередь ID кабин на станции; число кабин — cabins.count
} Station;

#endif
