#ifndef STANDALONE_H
#define STANDALONE_H

#include "components/model.h"
#include "components/status.h"
#include "ipc/shared_memory.h"
#include "ipc/sync.h"

// вариант с независимо запускаемыми программами-процессами
// каждая программа подключается к уже существующей разделяемой памяти
// и открывает именованные семафоры по известному имени
typedef struct StandaloneLink {
    SharedMemory memory;
    SharedModel *model;
    SyncSet sync;
} StandaloneLink;

// подключение к ресурсам, созданным управляющей программой cc_control
Status standalone_attach(StandaloneLink *link);

// отключение без удаления: ресурсы удаляет только создатель
void standalone_detach(StandaloneLink *link);

// сообщает управляющей программе, что участник занял своё место
Status standalone_announce(StandaloneLink *link);

// ожидание разрешения на запуск: управляющая программа дожидается
// регистрации всех участников и только потом открывает первый такт
Status standalone_wait_start(StandaloneLink *link);

#endif
