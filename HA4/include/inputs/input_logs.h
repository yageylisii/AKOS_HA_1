#ifndef INPUT_LOGS_H
#define INPUT_LOGS_H

#include <stdbool.h>
#include <stddef.h>

#include "components/status.h"

/*
буфер фиксирован: журнал входит в модель, которая передаётся по сети.
Рабочий день модели начинается в 09:00
*/
enum { LOG_BUFFER_SIZE = 65536, LOG_EVENT_SIZE = 512, SERVICE_START_SECONDS = 9 * 3600 };

/*
служебные поля стоят перед буфером: тогда при передаче модели
достаточно отправить её начало и занятую часть буфера
*/
typedef struct EventLog {
    size_t length;                // занято байт
    int event_time;               // модельное время группы
    bool has_events;              // в буфере есть неотображённые события
    bool printed_group;           // хотя бы одна группа уже выведена
    bool overflow;                // буфер переполнялся, часть событий потеряна
    char buffer[LOG_BUFFER_SIZE]; // накопленные события одной группы
} EventLog;

void log_init(EventLog *log);

// добавляет событие в группу текущего модельного времени
Status log_event(EventLog *log, int event_time, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

// выводит накопленную группу в out_fd и, если file_fd >= 0, дублирует в файл
// delay_ms — регулируемая задержка отображения перед следующей группой
Status log_flush(EventLog *log, int out_fd, int file_fd, int delay_ms);

/*
переносит события src в группу dst: так сервер собирает в одну группу
события, которые прислали разные участники за одну модельную секунду
*/
Status log_merge(EventLog *dst, const EventLog *src);

// форматирует время модели как ЧЧ:ММ:СС
void log_format_time(int model_time, char *buffer, size_t capacity);

// печатает строку в дескриптор целиком, повторяя при EINTR
Status log_write(int fd, const char *text, size_t length);

const char *station_name(int station_index);

#endif
