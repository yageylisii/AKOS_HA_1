#define _POSIX_C_SOURCE 200809L

#include "net/protocol.h"
#include "roles/client.h"

#include <stdlib.h>

// клиент-участник, кабина: высадка, посадка, двери, движение между станциями
int main(int argc, char **argv)
{
    return participant_run(argc, argv, ROLE_CABIN) == SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
