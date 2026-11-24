#define _POSIX_C_SOURCE 200809L

#include "net/protocol.h"
#include "roles/client.h"

#include <stdlib.h>

// клиент-участник, оператор канатной дороги: погода, неисправности, разрешение отправления
int main(int argc, char **argv)
{
    return participant_run(argc, argv, ROLE_OPERATOR) == SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
