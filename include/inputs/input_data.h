#ifndef INPUT_DATA_H
#define INPUT_DATA_H

#include "components/settings.h"
#include "components/status.h"

// запрашивает основные параметры и интервалы ветра, затем сохраняет их в Settings
// остальные поля не меняются, структура должна быть инициализирована вызывающим кодом
// возвращает SUCCESS при успехе, ERROR при конце ввода или ошибке ввода-вывода
// при неуспехе Settings не меняется
Status input_data(Settings *settings);

#endif
