#ifndef SETTINGS_H
#define SETTINGS_H

// ограничения модели: защита от огромных очередей и журналов
// все массивы в разделяемой памяти имеют фиксированный размер,
// поэтому границы обязаны быть константами времени компиляции
enum {
    MAX_WIND_INTERVALS = 3,
    MAX_CABINS = 12,
    MAX_CABIN_CAPACITY = 20,
    MAX_PASSENGERS = 200,
    MAX_WORK_TIME = 43200,
    MAX_TRAVEL_TIME = 1800,
    MAX_BREAKDOWN_DURATION = 300,
    MAX_ARRIVAL_INTERVAL = 3600,
    MAX_PASSENGERS_PER_INTERVAL = 20,
    MAX_BOARDING_TIME = 60,
    MAX_UNLOADING_TIME = 60,
    MAX_DRAIN_TIME = 43200,
    MAX_DISPLAY_DELAY_MS = 5000
};

#define MAX_BREAKDOWN_PROBABILITY 0.5

// интервал сильного ветра: [start; end) в секундах от начала работы в 09:00
typedef struct BadWeather {
    int start;
    int end;
} BadWeather;

typedef struct Settings {
    int cabines;                 // всего кабин
    int capacity;                // вместимость одной кабины
    int max_passengers;          // предел числа пассажиров за день на обе станции
    int work_time;               // длительность приёма пассажиров с 09:00, секунды
    int time_between_stations;   // время пути в любом направлении, секунды
    int breakdown_duration;      // длительность остановки из-за неисправности, секунды
    int arrival_interval;        // интервал появления групп пассажиров, секунды
    int passengers_per_interval; // пассажиров за интервал суммарно на обеих станциях
    int boarding_time;           // посадка одного пассажира, секунды
    int unloading_time;          // высадка одного пассажира, секунды

    double vip_probability;      // доля VIP: от 0 до 1
    double breakdown_probability;// вероятность поломки за модельную минуту: от 0 до 0.5

    BadWeather wind[MAX_WIND_INTERVALS]; // погодные пороги: интервалы сильного ветра
    int wind_count;                      // сколько интервалов заполнено

    int display_delay_ms;        // регулируемая задержка отображения между группами событий
    unsigned int seed;           // воспроизводимое начальное значение генератора
} Settings;

#endif
