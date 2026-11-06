#ifndef STANDALONE_H
#define STANDALONE_H

#include "components/model.h"
#include "components/status.h"
#include "ipc/shared_memory.h"
#include "ipc/sync.h"

// Вариант с независимо запускаемыми программами-процессами.
// Каждая программа подключается к уже существующей разделяемой памяти
// и открывает именованные семафоры по известному имени.
typedef struct StandaloneLink {
    SharedMemory memory;
    SharedModel *model;
    SyncSet sync;
} StandaloneLink;

// Подключение к ресурсам, созданным управляющей программой cc_control.
Status standalone_attach(StandaloneLink *link);

// Отключение без удаления: ресурсы удаляет только создатель.
void standalone_detach(StandaloneLink *link);

// Сообщает управляющей программе, что участник занял своё место.
Status standalone_announce(StandaloneLink *link);

// Ожидание разрешения на запуск: управляющая программа дожидается
// регистрации всех участников и только потом открывает первый такт.
Status standalone_wait_start(StandaloneLink *link);

#endif
