#define _POSIX_C_SOURCE 200809L

#include "roles/client.h"

#include <stdlib.h>

// клиент-наблюдатель: журнал всей системы и сводная таблица, подключается в любой момент
int main(int argc, char **argv)
{
    return observer_run(argc, argv) == SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
