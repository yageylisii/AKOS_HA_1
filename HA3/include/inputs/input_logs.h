#ifndef INPUT_LOGS_H
#define INPUT_LOGS_H

#include <stdbool.h>
#include <stddef.h>

#include "components/status.h"

// журнал размещается в разделяемой памяти, поэтому буфер фиксирован:
// realloc из HA1 здесь невозможен, память общая для всех процессов
// рабочий день модели начинается в 09:00
enum { LOG_BUFFER_SIZE = 65536, LOG_EVENT_SIZE = 512, SERVICE_START_SECONDS = 9 * 3600 };

typedef struct EventLog {
    char buffer[LOG_BUFFER_SIZE]; // накопленные события одной группы
    size_t length;                // занято байт
    int event_time;               // модельное время группы
    bool has_events;              // в буфере есть неотображённые события
    bool printed_group;           // хотя бы одна группа уже выведена
    bool overflow;                // буфер переполнялся, часть событий потеряна
} EventLog;

void log_init(EventLog *log);

// добавляет событие в группу текущего модельного времени
// вызывается под мьютексом модели: буфер общий для всех процессов
Status log_event(EventLog *log, int event_time, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

// выводит накопленную группу в out_fd и, если file_fd >= 0, дублирует в файл
// delay_ms — регулируемая задержка отображения перед следующей группой
Status log_flush(EventLog *log, int out_fd, int file_fd, int delay_ms);

// печатает строку в дескриптор целиком, повторяя при EINTR
Status log_write(int fd, const char *text, size_t length);

const char *station_name(int station_index);

#endif
