// вариант на оценку 6
// тот же единый родительский процесс и та же разделяемая память POSIX,
// но согласование выполняют ИМЕНОВАННЫЕ POSIX-семафоры: они создаются
// вызовом sem_open по именам вида /akos46_named_0 и наследуются
// дочерними процессами при fork

#include "ipc/ipc_names.h"
#include "ipc/sync.h"
#include "roles/launcher.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    fprintf(stderr, "Канатная дорога: именованные POSIX-семафоры\n");

    Status result = run_forked_application(argc, argv, SYNC_NAMED,
                                           SHM_NAME_NAMED, SEM_PREFIX_NAMED);
    return result == SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
