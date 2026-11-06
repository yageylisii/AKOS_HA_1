#ifndef STATISTICS_H
#define STATISTICS_H

typedef struct Statistics {
    int accepted;         // принято пассажиров в систему
    int delivered;        // доставлено на станцию назначения
    int evacuated;        // эвакуировано пассажиров
    int rejected;         // отказов из-за заполнения кабины
    int trips;            // выполнено рейсов
    int breakdowns;       // технических остановок
    int wind_stops;       // остановок из-за сильного ветра
    long long total_wait; // суммарное ожидание доставленных пассажиров, секунды
} Statistics;

#endif
