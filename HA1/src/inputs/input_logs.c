#include "inputs/input_logs.h"
#include "asserts.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

const char *station_name(int station_id) {
    return station_id == 1 ? "🅰️" : station_id == 2 ? "🅱️" : "?";
}


// записываем текст в консоль с помощью дескриптора
static Status write_text(const char *text, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        ssize_t bytes_written = write(STDOUT_FILENO, text + offset, length - offset);
        if (bytes_written < 0 && errno == EINTR) continue;
        SOFT_ASSERT(bytes_written > 0, "Ошибка записи журнала", ERROR);
        offset += (size_t)bytes_written;
    }
    return SUCCESS;
}

Status log_flush(EventLog *log) {
    SOFT_ASSERT(log != NULL, "Не передан журнал", ERROR);

    if (!log->has_events) {
        return SUCCESS;
    }

    // Пауза перед следующей временной группой
    if (log->printed_group) {
        struct timespec remaining_delay = {.tv_sec = 1, .tv_nsec = 0};
        while (nanosleep(&remaining_delay, &remaining_delay) != 0) {
            SOFT_ASSERT(errno == EINTR, "Ошибка задержки между событиями", ERROR);
        }
    }
    if (write_text(log->buffer, log->length) != SUCCESS) return ERROR;

    // обнуляем лог
    log->length = 0;
    log->has_events = false;
    log->printed_group = true;
    return SUCCESS;
}

Status log_event(EventLog *log, int event_time, const char *format, ...) {
    SOFT_ASSERT(log != NULL && format != NULL && event_time >= 0, "Неверные параметры журнала", ERROR);
    char event[512];

    // работаем c %d и пр (в ...)
    va_list arguments;
    va_start(arguments, format);
    int event_length = vsnprintf(event, sizeof(event), format, arguments); // записываем аргументы из ... в format
    va_end(arguments);
    SOFT_ASSERT(event_length >= 0 && (size_t)event_length < sizeof(event), "Слишком длинное событие", ERROR);

    // выводим старую группу логов (что осталось)
    if (log->has_events && log->event_time != event_time) {
        if (log_flush(log) != SUCCESS){
            return ERROR;
        }
    }
    char header[64];
    int header_length = 0;
    if (!log->has_events) {
        long long seconds = 9LL * 3600 + event_time;
        header_length = snprintf(header, sizeof(header), "[%02lld:%02lld:%02lld]\n",
                                 seconds / 3600, seconds / 60 % 60, seconds % 60); // собирает заголовок вида [09:00:10]
        SOFT_ASSERT(header_length > 0 && (size_t)header_length < sizeof(header), "Ошибка времени журнала", ERROR);
    }

    // считаем сколько нужно  под запись данных в буффер
    size_t added_length = (size_t)header_length + (size_t)event_length + 3;
    SOFT_ASSERT(log->length <= SIZE_MAX - added_length, "Журнал слишком велик", ERROR);
    size_t required = log->length + added_length;

    // если нужного места не хватило - увеличиваем realoccom
    if (required > log->capacity) {
        char *new_buffer = realloc(log->buffer, required);
        SOFT_ASSERT(new_buffer != NULL, "Не удалось выделить память для журнала", ERROR);
        log->buffer = new_buffer;
        log->capacity = required;
    }
    // memcpy(куда, откуда, сколько)
    // добавляем заголовок (если есть)
    if (header_length > 0) {
        memcpy(log->buffer + log->length, header, (size_t)header_length);
        log->length += (size_t)header_length; // увелич чтобы следующая запись шла после заголовка
    }
    // добавляем 2 пробела
    memcpy(log->buffer + log->length, "  ", 2);
    log->length += 2;

    // добляем текст события
    memcpy(log->buffer + log->length, event, (size_t)event_length);
    log->length += (size_t)event_length;

    //добавляем перевод
    log->buffer[log->length++] = '\n';

    log->event_time = event_time;
    log->has_events = true;
    return SUCCESS;
}

void log_destroy(EventLog *log) {
    if (!log) return;
    free(log->buffer);
    *log = (EventLog){0};
}
