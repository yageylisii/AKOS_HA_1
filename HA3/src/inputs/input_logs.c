#define _POSIX_C_SOURCE 200809L

#include "inputs/input_logs.h"
#include "asserts.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

const char *station_name(int station_index)
{
    return station_index == 0 ? "A" : station_index == 1 ? "B" : "?";
}

Status log_write(int fd, const char *text, size_t length)
{
    SOFT_ASSERT(text != NULL, "Не передан текст журнала", ERROR);
    if (fd < 0 || length == 0) {
        return SUCCESS;
    }
    size_t offset = 0;
    while (offset < length) {
        ssize_t written = write(fd, text + offset, length - offset);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        SOFT_ASSERT(written > 0, "Ошибка записи журнала", ERROR);
        offset += (size_t)written;
    }
    return SUCCESS;
}

void log_init(EventLog *log)
{
    if (log == NULL) {
        return;
    }
    log->length = 0;
    log->event_time = 0;
    log->has_events = false;
    log->printed_group = false;
    log->overflow = false;
}

Status log_event(EventLog *log, int event_time, const char *format, ...)
{
    SOFT_ASSERT(log != NULL && format != NULL && event_time >= 0,
                "Неверные параметры журнала", ERROR);

    char event[LOG_EVENT_SIZE];
    va_list arguments;
    va_start(arguments, format);
    int event_length = vsnprintf(event, sizeof(event), format, arguments);
    va_end(arguments);
    SOFT_ASSERT(event_length >= 0 && (size_t)event_length < sizeof(event),
                "Слишком длинное событие", ERROR);

    // заголовок печатается один раз на группу событий одного модельного времени
    char header[64];
    int header_length = 0;
    if (!log->has_events) {
        long long seconds = (long long)SERVICE_START_SECONDS + event_time;
        header_length = snprintf(header, sizeof(header), "[%02lld:%02lld:%02lld]\n",
                                 seconds / 3600, seconds / 60 % 60, seconds % 60);
        SOFT_ASSERT(header_length > 0 && (size_t)header_length < sizeof(header),
                    "Ошибка времени журнала", ERROR);
    }

    size_t required = log->length + (size_t)header_length + (size_t)event_length + 3;
    if (required > LOG_BUFFER_SIZE) {
        // буфер в разделяемой памяти расширить нельзя: отмечаем потерю событий
        log->overflow = true;
        return ERROR;
    }

    if (header_length > 0) {
        memcpy(log->buffer + log->length, header, (size_t)header_length);
        log->length += (size_t)header_length;
    }
    memcpy(log->buffer + log->length, "  ", 2);
    log->length += 2;
    memcpy(log->buffer + log->length, event, (size_t)event_length);
    log->length += (size_t)event_length;
    log->buffer[log->length++] = '\n';

    log->event_time = event_time;
    log->has_events = true;
    return SUCCESS;
}

Status log_flush(EventLog *log, int out_fd, int file_fd, int delay_ms)
{
    SOFT_ASSERT(log != NULL, "Не передан журнал", ERROR);

    if (!log->has_events) {
        return SUCCESS;
    }

    // регулируемая задержка отображения перед следующей группой событий
    if (log->printed_group && delay_ms > 0) {
        struct timespec remaining = {
            .tv_sec = delay_ms / 1000,
            .tv_nsec = (long)(delay_ms % 1000) * 1000000L
        };
        while (nanosleep(&remaining, &remaining) != 0) {
            SOFT_ASSERT(errno == EINTR, "Ошибка задержки между событиями", ERROR);
        }
    }

    if (log_write(out_fd, log->buffer, log->length) != SUCCESS) {
        return ERROR;
    }
    if (file_fd >= 0 && log_write(file_fd, log->buffer, log->length) != SUCCESS) {
        return ERROR;
    }

    if (log->overflow) {
        const char *warning = "  ! буфер журнала переполнен, часть событий не выведена\n";
        log_write(out_fd, warning, strlen(warning));
        log->overflow = false;
    }

    log->length = 0;
    log->has_events = false;
    log->printed_group = true;
    return SUCCESS;
}
