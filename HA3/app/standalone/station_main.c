// программа семейства пассажиров станции
// запускается независимо: ./cc_station A или ./cc_station B
// процесс отвечает за появление пассажиров на своей станции и ведение
// её очередей. Данные передаются через разделяемую память POSIX,
// согласование — через именованные POSIX-семафоры

#define _POSIX_C_SOURCE 200809L

#include "roles/clock.h"
#include "roles/roles.h"
#include "roles/standalone.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// станция задаётся буквой A или B либо номером 1 или 2
static int parse_station(const char *text)
{
    if (text == NULL) {
        return -1;
    }
    if (strcmp(text, "A") == 0 || strcmp(text, "a") == 0 || strcmp(text, "1") == 0) {
        return 0;
    }
    if (strcmp(text, "B") == 0 || strcmp(text, "b") == 0 || strcmp(text, "2") == 0) {
        return 1;
    }
    return -1;
}

static Status register_station(StandaloneLink *link, int station_index)
{
    if (sync_wait(&link->sync, SEM_MODEL) != SUCCESS) {
        return ERROR;
    }

    Status result = SUCCESS;
    Station *station = &link->model->stations[station_index];
    if (station->registered) {
        fprintf(stderr, "Станция %s уже обслуживается другим процессом\n",
                station_name(station_index));
        result = ERROR;
    } else {
        station->registered = true;
        station->pid = getpid();
        ++link->model->registered_stations;
    }

    sync_post(&link->sync, SEM_MODEL);
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Использование: %s A|B\n", argv[0]);
        return EXIT_FAILURE;
    }

    int station_index = parse_station(argv[1]);
    if (station_index < 0) {
        fprintf(stderr, "Неизвестная станция: %s. Допустимы A и B\n", argv[1]);
        return EXIT_FAILURE;
    }

    install_signal_handlers();

    StandaloneLink link;
    if (standalone_attach(&link) != SUCCESS) {
        return EXIT_FAILURE;
    }
    if (register_station(&link, station_index) != SUCCESS ||
        standalone_announce(&link) != SUCCESS) {
        standalone_detach(&link);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "Станция %s подключена (pid %ld), ожидание запуска модели\n",
            station_name(station_index), (long)getpid());

    Status result = standalone_wait_start(&link);
    if (result == SUCCESS) {
        fprintf(stderr, "Станция %s начала принимать пассажиров\n",
                station_name(station_index));
        result = worker_loop(link.model, &link.sync, WORKER_STATION, station_index);
        fprintf(stderr, "Станция %s завершила работу\n", station_name(station_index));
    }

    standalone_detach(&link);
    return result == SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
