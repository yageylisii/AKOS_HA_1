#ifndef STATION_H
#define STATION_H

#include <stdbool.h>
#include <sys/types.h>

#include "components/queue.h"

// как и в HA1, на станции три места для кабин
enum { STATION_CABIN_CAPACITY = 3 };

typedef struct Station {
    int id;                    // 1 или 2 для вывода, индекс в массиве — 0 или 1
    IntQueue vip;              // очередь VIP-пассажиров
    IntQueue regular;          // очередь обычных пассажиров
    IntQueue waiting_cabins;   // кабины перед станцией, двери закрыты
    IntQueue cabins;           // кабины на местах станции, не более STATION_CABIN_CAPACITY
    unsigned int rng;          // отдельный поток случайных чисел станции
    pid_t pid;                 // процесс семейства пассажиров этой станции
    bool registered;           // процесс станции подключился к модели
} Station;

#endif
