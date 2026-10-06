#ifndef INPUT_DATA_H
#define INPUT_DATA_H

#include "components/settings.h"
#include "components/status.h"

// Запрашивает двенадцать параметров и сохраняет их в Settings.
// Остальные поля не меняются. Структура должна быть инициализирована вызывающим кодом.
// Возвращает SUCCESS при успехе, ERROR при конце ввода или ошибке ввода-вывода.
// При неуспехе Settings не меняется.
Status input_data(Settings *settings);

#endif
