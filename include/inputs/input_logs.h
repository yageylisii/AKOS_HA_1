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

// Журнал первоначально обнулён
// События одного времени собираются в одну группу
Status log_event(EventLog *log, int event_time, const char *format, ...);
Status log_flush(EventLog *log);
void log_destroy(EventLog *log);
const char *station_name(int station_id);

#endif
