#ifndef SETTINGS_H
#define SETTINGS_H


// ограничения модели: защита от огромных очередей и журналов
enum {
    MAX_WIND_INTERVALS = 20,
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
    MAX_DRAIN_TIME = 43200
};
#define MAX_BREAKDOWN_PROBABILITY 0.5
// начинаем в 9:00

typedef struct BadWeather {
    int start; // Начало порыва в 
    int end;   // Конец порыва
} BadWeather;

typedef struct Settings {
    int cabines;    // всего кабин
    int boarding_time; // Посадка одного пассажира, секунды
    int unloading_time; // Высадка одного пассажира, секунды
    double vip_probability; // Доля VIP: от 0 до 1
    int capacity;    // Вместимость
    int time_between_stations; // время в пути в секундах
    int work_time; // Длительность приёма пассажиров с 09:00, задаётся с консоли в секундах

    int max_passengers; // Максимум появившихся пассажиров за день суммарно на обеих станциях
    int arrival_interval; // Фиксированный интервал появления пассажиров, секунды
    int passengers_per_interval; // Сколько пассажиров появляется за интервал суммарно на обеих станциях
    // Например: arrival_interval = 60, passengers_per_interval = 5.
    // VIP входят в общее количество; priority влияет только на порядок посадки.
    // Последняя группа ограничивается оставшимся лимитом max_passengers.

    BadWeather wind[MAX_WIND_INTERVALS];
    int wind_count; // Сколько интервалов заполнено

    double breakdown_probability; // Вероятность поломки за модельную минуту: от 0.0 до 1.0
    int breakdown_duration; // Длительность остановки из-за поломки в секундах

} Settings;




#endif
