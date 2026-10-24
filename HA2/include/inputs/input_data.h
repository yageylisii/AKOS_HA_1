#ifndef INPUT_DATA_H
#define INPUT_DATA_H

#include "components/settings.h"
#include "components/status.h"

// структура должна быть инициализирована вызывающим кодом
// при ошибке или конце ввода возвращается ERROR и настройки не меняются

// собирает настройки модели с помощью вопросов в консоли
Status input_data(Settings *settings);

#endif
