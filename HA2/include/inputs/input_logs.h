#ifndef INPUT_LOGS_H
#define INPUT_LOGS_H

#include <stdbool.h>
#include <stddef.h>
#include "components/status.h"

typedef struct EventLog {
    char *buffer;
    size_t length;
    size_t capacity;
    int event_time;
    bool has_events;
    bool printed_group;
} EventLog;

// добавляет событие в журнал текущего модельного времени
Status log_event(EventLog *log, int event_time, const char *format, ...);
// выводит накопленную группу событий с задержкой между группами
Status log_flush(EventLog *log);
// освобождает буфер журнала
void log_destroy(EventLog *log);
// возвращает обозначение станции по её номеру
const char *station_name(int station_id);

#endif
