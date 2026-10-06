#include "inputs/input_data.h"
#include "asserts.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


// пишем в консоль через дескриптор write(STDOUT_FILENO, text, remaining)
static Status write_text(const char *text) {
    size_t remaining = strlen(text);
    while (remaining > 0) {
        ssize_t written = write(STDOUT_FILENO, text, remaining);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return ERROR;
        }
        text += written;
        remaining -= (size_t)written;
    }
    return SUCCESS;
}

// записываем в буфер строку которую ввел пользователь
static Status read_line(char *buffer, size_t capacity) {
    size_t length = 0;
    int too_long = 0;
    for (;;) {
        // читаем побайтово консоль в ch
        char ch;
        ssize_t result = read(STDIN_FILENO, &ch, 1);

        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0) {
            return ERROR;
        }
        if (result == 0 || ch == '\n') {
            if (result == 0 && length == 0 && !too_long) {
                return ERROR;
            }
            buffer[length] = '\0';
            return too_long ? INVALID_INPUT : SUCCESS;
        }
        if (ch == '\0' || length + 1 >= capacity) {
            too_long = 1;
        } else {
            buffer[length++] = ch;
        }
    }
}

// проверка ввода пользователя в соответсвиями с ограничениями
static Status parse_integer(const char *buffer, Status status, int minimum, int maximum, int *value) {
    SOFT_ASSERT(status == SUCCESS, "Слишком длинная строка или недопустимый символ", ERROR);

    char *end;
    errno = 0;
    long parsed = strtol(buffer, &end, 10);
    SOFT_ASSERT(end != buffer, "Введите целое число", ERROR);
    SOFT_ASSERT(errno != ERANGE, "Число слишком велико по модулю", ERROR);
    while (isspace((unsigned char)*end)) { // скип пробелы
        ++end;
    }
    SOFT_ASSERT(*end == '\0', "После числа обнаружены лишние символы", ERROR);
    SOFT_ASSERT(parsed <= INT_MAX, "Число превышает диапазон int", ERROR);
    SOFT_ASSERT(parsed >= minimum,
                minimum == 0 ? "Число должно быть неотрицательным"
                             : "Число должно быть положительным", ERROR);
    SOFT_ASSERT(parsed <= maximum, "Превышен допустимый максимум", ERROR);
    *value = (int)parsed;
    return SUCCESS;
}

static Status ask_integer(const char *prompt, int minimum, int maximum, int *value) {
    char question[256];
    int length = snprintf(question, sizeof(question), "%s [%d–%d]: ", prompt, minimum, maximum);
    SOFT_ASSERT(length > 0 && (size_t)length < sizeof(question), "Слишком длинный вопрос", ERROR);
    for (;;) {
        char buffer[128];
        if (write_text(question) != SUCCESS) { // вывод в консоль
            return ERROR;
        }
        Status status = read_line(buffer, sizeof(buffer)); // сохранения в буфер
        if (status == ERROR) {
            return ERROR;
        }
        // проверка чисел и запись
        if (parse_integer(buffer, status, minimum, maximum, value) == SUCCESS) {
            return SUCCESS;
        }
    }
}


// вероятность вводится долей: 0.2 - парсим
static Status parse_probability(const char *buffer, Status status, double maximum, double *value) {
    SOFT_ASSERT(status == SUCCESS, "Слишком длинная строка или недопустимый символ", ERROR);
    char *end;
    errno = 0;
    double parsed = strtod(buffer, &end);
    SOFT_ASSERT(end != buffer, "Введите число от 0 до 1", ERROR);
    SOFT_ASSERT(errno != ERANGE && isfinite(parsed), "Неверное значение вероятности", ERROR);
    while (isspace((unsigned char)*end)) ++end;
    SOFT_ASSERT(*end == '\0', "После числа обнаружены лишние символы; используйте точку", ERROR);
    SOFT_ASSERT(parsed >= 0.0 && parsed <= maximum, "Вероятность вне указанного диапазона", ERROR);
    *value = parsed;
    return SUCCESS;
}

static Status ask_probability(const char *prompt, double maximum, double *value) {
    for (;;) {
        char buffer[128];
        if (write_text(prompt) != SUCCESS) return ERROR;
        Status status = read_line(buffer, sizeof(buffer));
        if (status == ERROR) return ERROR;
        if (parse_probability(buffer, status, maximum, value) == SUCCESS) return SUCCESS;
    }
}

// гланая функция сбора информации с консоли с помощью контрольныйх вопросов
Status input_data(Settings *settings) {
    SOFT_ASSERT(settings != NULL, "Не передана структура настроек", ERROR);
    Settings entered = *settings;
    if (ask_integer("Количество кабин", 1, MAX_CABINS, &entered.cabines) != SUCCESS ||
        ask_integer("Вместимость одной кабины", 1, MAX_CABIN_CAPACITY, &entered.capacity) != SUCCESS ||
        ask_integer("Максимальное количество пассажиров за день (обе станции)",
                     0, MAX_PASSENGERS, &entered.max_passengers) != SUCCESS ||
        ask_integer("Длительность работы с 09:00 в секундах", 1, MAX_WORK_TIME, &entered.work_time) != SUCCESS ||
        ask_integer("Время пути между станциями в секундах",
                     1, MAX_TRAVEL_TIME, &entered.time_between_stations) != SUCCESS ||
        ask_integer("Длительность остановки из-за поломки в секундах",
                     1, MAX_BREAKDOWN_DURATION, &entered.breakdown_duration) != SUCCESS ||
        ask_integer("Интервал появления пассажиров в секундах",
                     1, MAX_ARRIVAL_INTERVAL, &entered.arrival_interval) != SUCCESS ||
        ask_integer("Пассажиров за интервал (суммарно на обеих станциях)",
                     1, MAX_PASSENGERS_PER_INTERVAL, &entered.passengers_per_interval) != SUCCESS ||
        ask_integer("Время посадки одного пассажира в секундах",
                     1, MAX_BOARDING_TIME, &entered.boarding_time) != SUCCESS ||
        ask_integer("Время высадки одного пассажира в секундах",
                     1, MAX_UNLOADING_TIME, &entered.unloading_time) != SUCCESS ||
        ask_probability("Вероятность VIP-билета (0–1, например 0.2): ",
                         1.0, &entered.vip_probability) != SUCCESS ||
        ask_probability("Вероятность поломки за модельную минуту (0–0.5): ",
                         MAX_BREAKDOWN_PROBABILITY, &entered.breakdown_probability) != SUCCESS) {
        return ERROR;
    }
    *settings = entered; // если весь ввод успешен, то заполняем ориг
    return SUCCESS;
}
