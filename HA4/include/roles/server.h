#ifndef SERVER_H
#define SERVER_H

#include "components/settings.h"
#include "components/status.h"
#include "inputs/input_data.h"

/*
сервер канатной дороги. Хранит модель, ведёт модельное время и по очереди
выдаёт участникам разрешение такта, как ведущий процесс HA3. Вместо
семафоров — сообщения TCP, вместо разделяемой памяти — копия модели,
которая уходит участнику и возвращается изменённой
*/
Status server_run(const Settings *settings, const RunOptions *options);

#endif
