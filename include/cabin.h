#ifndef CABIN_H
#define CABIN_H


typedef struct Cabin {
    int id;          // Номер кабины
    int capacity;    // Вместимость
    int passengers;  // Количество пассажиров
    int way_time; // сколько едет кабина между станциями
    bool doors_open;  // 1 — двери открыты, 0 — закрыты
} Cabin ;




#endif