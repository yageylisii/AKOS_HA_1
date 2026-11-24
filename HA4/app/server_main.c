#define _POSIX_C_SOURCE 200809L

#include "components/settings.h"
#include "inputs/input_data.h"
#include "roles/server.h"

#include <stdio.h>
#include <stdlib.h>

// сервер канатной дороги: хранит модель и ведёт модельное время
int main(int argc, char **argv)
{
    RunOptions options;
    if (cli_parse(argc, argv, &options) != SUCCESS) {
        cli_usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (options.show_help) {
        cli_usage(argv[0]);
        return EXIT_SUCCESS;
    }
    if (!options.has_port) {
        fprintf(stderr, "Не задан порт сервера\n");
        cli_usage(argv[0]);
        return EXIT_FAILURE;
    }

    // настройки читаются до открытия порта: клиентам сразу известно число кабин
    Settings settings = {0};
    Status status = options.config_path != NULL
                        ? input_data_from_file(options.config_path, &settings)
                        : input_data(&settings);
    if (status != SUCCESS || settings_apply_options(&settings, &options) != SUCCESS) {
        fprintf(stderr, "Настройки не заданы, сервер не запущен\n");
        return EXIT_FAILURE;
    }

    return server_run(&settings, &options) == SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
