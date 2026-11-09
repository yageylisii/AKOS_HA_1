// вариант на оценку 4-5
// единый родительский процесс запускает требуемое число дочерних процессов
// обмен данными ведётся через разделяемую память в стандарте POSIX,
// согласование — через НЕИМЕНОВАННЫЕ POSIX-семафоры, размещённые в той же разделяемой памяти

#include "ipc/ipc_names.h"
#include "ipc/sync.h"
#include "roles/launcher.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    fprintf(stderr,
            "Канатная дорога: неименованные POSIX-семафоры в разделяемой памяти\n");

    Status result = run_forked_application(argc, argv, SYNC_UNNAMED,
                                           SHM_NAME_UNNAMED, SEM_PREFIX_NAMED);
    return result == SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
