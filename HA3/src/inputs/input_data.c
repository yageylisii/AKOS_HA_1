#define _POSIX_C_SOURCE 200809L

#include "inputs/input_data.h"
#include "asserts.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// значения по умолчанию, если какой-то параметр не задан
enum { DEFAULT_DELAY_MS = 300 };

// источник ответов: консоль или конфигурационный файл
// весь ввод-вывод идёт через дескрипторы и системные вызовы read и write
static int g_input_fd = STDIN_FILENO;
static bool g_quiet = false;

// вопросы выводим в stderr, чтобы перенаправление stdout сохраняло только журнал
static Status write_text(const char *text)
{
    if (g_quiet) {
        return SUCCESS;
    }
    size_t remaining = strlen(text);
    while (remaining > 0) {
        ssize_t written = write(STDERR_FILENO, text, remaining);
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

// читаем строку побайтово: один ответ — одна строка
static Status read_line(char *buffer, size_t capacity)
{
    size_t length = 0;
    bool too_long = false;
    for (;;) {
        char symbol;
        ssize_t result = read(g_input_fd, &symbol, 1);

        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0) {
            return ERROR;
        }
        if (result == 0 || symbol == '\n') {
            if (result == 0 && length == 0 && !too_long) {
                return ERROR;
            }
            buffer[length] = '\0';
            return too_long ? INVALID_INPUT : SUCCESS;
        }
        if (symbol == '\r') {
            continue;
        }
        if (symbol == '\0' || length + 1 >= capacity) {
            too_long = true;
        } else {
            buffer[length++] = symbol;
        }
    }
}

// пропускаем пустые строки и строки комментариев конфигурационного файла
static Status read_value_line(char *buffer, size_t capacity)
{
    for (;;) {
        Status status = read_line(buffer, capacity);
        if (status == ERROR) {
            return ERROR;
        }
        char *cursor = buffer;
        while (isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor == '\0' || *cursor == '#') {
            if (g_input_fd == STDIN_FILENO) {
                return status; // в консоли пустой ответ считаем ошибкой ввода
            }
            continue; // комментарий пропускаем, даже если он длиннее буфера
        }
        if (status != SUCCESS) {
            return status;
        }
        // комментарий после значения: 60   # время пути
        char *comment = strchr(cursor, '#');
        if (comment != NULL) {
            *comment = '\0';
        }
        if (cursor != buffer) {
            memmove(buffer, cursor, strlen(cursor) + 1);
        }
        return SUCCESS;
    }
}

static Status parse_integer(const char *buffer, Status status, int minimum, int maximum, int *value)
{
    SOFT_ASSERT(status == SUCCESS, "Слишком длинная строка или недопустимый символ", ERROR);

    char *end;
    errno = 0;
    long parsed = strtol(buffer, &end, 10);
    SOFT_ASSERT(end != buffer, "Введите целое число", ERROR);
    SOFT_ASSERT(errno != ERANGE, "Число слишком велико по модулю", ERROR);
    while (isspace((unsigned char)*end)) {
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

static Status ask_integer(const char *prompt, int minimum, int maximum, int *value)
{
    char question[256];
    int length = snprintf(question, sizeof(question), "%s [%d-%d]: ", prompt, minimum, maximum);
    SOFT_ASSERT(length > 0 && (size_t)length < sizeof(question), "Слишком длинный вопрос", ERROR);

    for (;;) {
        char buffer[256];
        if (write_text(question) != SUCCESS) {
            return ERROR;
        }
        Status status = read_value_line(buffer, sizeof(buffer));
        if (status == ERROR) {
            return ERROR;
        }
        if (parse_integer(buffer, status, minimum, maximum, value) == SUCCESS) {
            return SUCCESS;
        }
        // в файле повторять вопрос бессмысленно: данные уже неверные
        SOFT_ASSERT(g_input_fd == STDIN_FILENO, "Неверное значение в конфигурационном файле", ERROR);
    }
}

static Status parse_probability(const char *buffer, Status status, double maximum, double *value)
{
    SOFT_ASSERT(status == SUCCESS, "Слишком длинная строка или недопустимый символ", ERROR);

    char *end;
    errno = 0;
    double parsed = strtod(buffer, &end);
    SOFT_ASSERT(end != buffer, "Введите число от 0 до 1", ERROR);
    SOFT_ASSERT(errno != ERANGE && isfinite(parsed), "Неверное значение вероятности", ERROR);
    while (isspace((unsigned char)*end)) {
        ++end;
    }
    SOFT_ASSERT(*end == '\0', "После числа лишние символы; используйте точку", ERROR);
    SOFT_ASSERT(parsed >= 0.0 && parsed <= maximum, "Вероятность вне указанного диапазона", ERROR);
    *value = parsed;
    return SUCCESS;
}

static Status ask_probability(const char *prompt, double maximum, double *value)
{
    for (;;) {
        char buffer[256];
        if (write_text(prompt) != SUCCESS) {
            return ERROR;
        }
        Status status = read_value_line(buffer, sizeof(buffer));
        if (status == ERROR) {
            return ERROR;
        }
        if (parse_probability(buffer, status, maximum, value) == SUCCESS) {
            return SUCCESS;
        }
        SOFT_ASSERT(g_input_fd == STDIN_FILENO, "Неверное значение в конфигурационном файле", ERROR);
    }
}

// погодные пороги: интервалы сильного ветра, при которых движение запрещено
static Status ask_wind_intervals(Settings *settings)
{
    if (ask_integer("Количество интервалов сильного ветра", 0, MAX_WIND_INTERVALS,
                    &settings->wind_count) != SUCCESS) {
        return ERROR;
    }

    int maximum_time = settings->work_time + MAX_DRAIN_TIME;
    for (int index = 0; index < settings->wind_count; ++index) {
        char prompt[128];
        int length = snprintf(prompt, sizeof(prompt),
                              "Ветер %d: начало в секундах от 09:00", index + 1);
        SOFT_ASSERT(length > 0 && (size_t)length < sizeof(prompt), "Слишком длинный вопрос", ERROR);
        if (ask_integer(prompt, 0, maximum_time - 1, &settings->wind[index].start) != SUCCESS) {
            return ERROR;
        }

        length = snprintf(prompt, sizeof(prompt),
                          "Ветер %d: окончание в секундах от 09:00", index + 1);
        SOFT_ASSERT(length > 0 && (size_t)length < sizeof(prompt), "Слишком длинный вопрос", ERROR);
        if (ask_integer(prompt, settings->wind[index].start + 1, maximum_time,
                        &settings->wind[index].end) != SUCCESS) {
            return ERROR;
        }
    }
    return SUCCESS;
}

// главный сбор настроек: порядок вопросов одинаков для консоли и файла
static Status collect_settings(Settings *settings)
{
    SOFT_ASSERT(settings != NULL, "Не передана структура настроек", ERROR);

    Settings entered = *settings;
    if (ask_integer("Количество кабин", 1, MAX_CABINS, &entered.cabines) != SUCCESS ||
        ask_integer("Вместимость одной кабины", 1, MAX_CABIN_CAPACITY, &entered.capacity) != SUCCESS ||
        ask_integer("Максимальное количество пассажиров за день (обе станции)",
                    0, MAX_PASSENGERS, &entered.max_passengers) != SUCCESS ||
        ask_integer("Длительность работы с 09:00 в секундах",
                    1, MAX_WORK_TIME, &entered.work_time) != SUCCESS ||
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
        ask_probability("Вероятность VIP-билета (0-1, например 0.2): ",
                        1.0, &entered.vip_probability) != SUCCESS ||
        ask_probability("Вероятность поломки за модельную минуту (0-0.5): ",
                        MAX_BREAKDOWN_PROBABILITY, &entered.breakdown_probability) != SUCCESS ||
        ask_wind_intervals(&entered) != SUCCESS) {
        return ERROR;
    }

    *settings = entered;
    return SUCCESS;
}

Status input_data(Settings *settings)
{
    g_input_fd = STDIN_FILENO;
    g_quiet = false;
    return collect_settings(settings);
}

Status input_data_from_file(const char *path, Settings *settings)
{
    SOFT_ASSERT(path != NULL && settings != NULL, "Не передан путь конфигурации", ERROR);

    int descriptor = open(path, O_RDONLY);
    if (descriptor == -1) {
        perror(path);
        return ERROR;
    }

    g_input_fd = descriptor;
    g_quiet = true;
    Status result = collect_settings(settings);
    g_input_fd = STDIN_FILENO;
    g_quiet = false;
    close(descriptor);

    SOFT_ASSERT(result == SUCCESS, "Конфигурационный файл заполнен не полностью", ERROR);
    return SUCCESS;
}

void cli_usage(const char *program)
{
    fprintf(stderr,
            "Использование: %s [параметры]\n"
            "  --config ФАЙЛ   прочитать настройки из файла вместо вопросов\n"
            "  --log ФАЙЛ      сохранить журнал работы (по умолчанию log.txt)\n"
            "  --seed ЧИСЛО    начальное значение генератора для повторяемого прогона\n"
            "  --delay МС      задержка отображения между группами событий\n"
            "  --no-delay      вывод без задержки\n"
            "  --help          эта справка\n",
            program);
}

static Status parse_unsigned_argument(const char *text, long maximum, long *value)
{
    char *end;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    SOFT_ASSERT(end != text && *end == '\0', "Аргумент должен быть числом", ERROR);
    SOFT_ASSERT(errno != ERANGE && parsed >= 0 && parsed <= maximum,
                "Аргумент вне допустимого диапазона", ERROR);
    *value = parsed;
    return SUCCESS;
}

Status cli_parse(int argc, char **argv, RunOptions *options)
{
    SOFT_ASSERT(options != NULL, "Не передана структура параметров запуска", ERROR);

    *options = (RunOptions){0};
    options->log_path = "log.txt";
    options->delay_ms = DEFAULT_DELAY_MS;

    for (int index = 1; index < argc; ++index) {
        const char *argument = argv[index];

        if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
            options->show_help = true;
            return SUCCESS;
        }
        if (strcmp(argument, "--no-delay") == 0) {
            options->delay_ms = 0;
            options->has_delay = true;
            continue;
        }

        SOFT_ASSERT(index + 1 < argc, "У параметра отсутствует значение", ERROR);
        const char *value = argv[++index];

        if (strcmp(argument, "--config") == 0) {
            options->config_path = value;
        } else if (strcmp(argument, "--log") == 0) {
            options->log_path = value;
        } else if (strcmp(argument, "--seed") == 0) {
            long parsed = 0;
            if (parse_unsigned_argument(value, INT_MAX, &parsed) != SUCCESS) {
                return ERROR;
            }
            options->seed = (unsigned int)parsed;
            options->has_seed = true;
        } else if (strcmp(argument, "--delay") == 0) {
            long parsed = 0;
            if (parse_unsigned_argument(value, MAX_DISPLAY_DELAY_MS, &parsed) != SUCCESS) {
                return ERROR;
            }
            options->delay_ms = (int)parsed;
            options->has_delay = true;
        } else {
            fprintf(stderr, "Неизвестный параметр: %s\n", argument);
            return ERROR;
        }
    }
    return SUCCESS;
}

Status settings_apply_options(Settings *settings, const RunOptions *options)
{
    SOFT_ASSERT(settings != NULL && options != NULL, "Не переданы настройки", ERROR);

    settings->display_delay_ms = options->delay_ms;
    // без явного значения берём время запуска: прогоны будут разными
    settings->seed = options->has_seed ? options->seed : (unsigned int)time(NULL);
    if (settings->seed == 0) {
        settings->seed = 1;
    }
    return SUCCESS;
}
