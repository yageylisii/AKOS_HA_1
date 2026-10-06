#ifndef STATISTICS_H
#define STATISTICS_H

typedef struct Statistics {
    int accepted;  // Принято пассажиров в систему
    int delivered; // Доставлено на станцию назначения
    int evacuated; // Эвакуировано пассажиров
} Statistics;

#endif
