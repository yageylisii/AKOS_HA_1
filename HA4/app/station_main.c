#define _POSIX_C_SOURCE 200809L

#include "net/protocol.h"
#include "roles/client.h"

#include <stdlib.h>

// клиент-участник, семейство пассажиров станции A или B: появление пассажиров и их очереди
int main(int argc, char **argv)
{
    return participant_run(argc, argv, ROLE_STATION) == SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
