#ifndef STATION_H
#define STATION_H


static const int MAX_QUEUE = 50;

// время между станциями фиксировано - задается с консоли 


typedef struct Station {
    Queue vip; // aka випка очередь
    Queue regular;  // теккущая очередь обычных (холопы)
    int way_time;
    bool arrived_cabine;  // 1 — кабина на станции, 0 - иначе
} Station;


// пассажир на станции и место в очереди 
typedef struct Passenger {
    int id;
    int priority;
} Passenger;


// очередь на станции
typedef struct Queue {
    Passenger data[MAX_QUEUE];
    int head;  // Индекс первого пассажира
    int tail;  // Индекс для добавления следующего
    int count; // Число пассажиров в очереди
} Queue;




#endif