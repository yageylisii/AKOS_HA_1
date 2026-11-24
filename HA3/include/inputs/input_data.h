#ifndef INPUT_DATA_H
#define INPUT_DATA_H

#include <stdbool.h>

#include "components/settings.h"
#include "components/status.h"

// параметры запуска, задаваемые аргументами командной строки
typedef struct RunOptions {
    const char *config_path; // конфигурационный файл вместо вопросов в консоли
    const char *log_path;    // куда сохранить журнал по завершении работы
    unsigned int seed;       // воспроизводимое начальное значение генератора
    bool has_seed;
    int delay_ms;            // регулируемая задержка отображения
    bool has_delay;
    bool show_help;
} RunOptions;

// разбор аргументов: --config, --log, --seed, --delay, --no-delay, --help
Status cli_parse(int argc, char **argv, RunOptions *options);
void cli_usage(const char *program);

// запрашивает параметры в консоли: по одному вопросу на строку, как в HA1
// возвращает ERROR при конце ввода, при неуспехе Settings не меняется
Status input_data(Settings *settings);

// читает те же параметры в том же порядке из конфигурационного файла
Status input_data_from_file(const char *path, Settings *settings);

// применяет значения из командной строки поверх прочитанных настроек
Status settings_apply_options(Settings *settings, const RunOptions *options);

#endif
