#ifndef ASSERTS_H
#define ASSERTS_H

#include <stdio.h>

// мягкая проверка: сообщение об ошибке в stderr и выход из функции с кодом ret
#define SOFT_ASSERT(cond, err, ret)                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "Condition: %s failed, error: %s\n",        \
                    #cond, err);                                        \
            return (ret);                                               \
        }                                                               \
    } while (0)

// тот же смысл, но без возвращаемого значения (для void-функций)
#define SOFT_ASSERT_VOID(cond, err)                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "Condition: %s failed, error: %s\n",        \
                    #cond, err);                                        \
            return;                                                     \
        }                                                               \
    } while (0)

// завершает функцию, если вложенный вызов вернул не SUCCESS
// убирает повторяющиеся проверки вокруг операций с семафорами
#define TRY(expression)                                                 \
    do {                                                                \
        if ((expression) != SUCCESS) {                                  \
            return ERROR;                                               \
        }                                                               \
    } while (0)

#endif
