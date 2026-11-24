#ifndef LAUNCHER_H
#define LAUNCHER_H

#include "components/status.h"
#include "ipc/sync.h"

// общий запуск варианта с единым родительским процессом
// отличие двух программ только в способе создания семафоров:
// mode = SYNC_UNNAMED или mode = SYNC_NAMED
Status run_forked_application(int argc, char **argv, int mode,
                              const char *shm_name, const char *sem_prefix);

#endif
