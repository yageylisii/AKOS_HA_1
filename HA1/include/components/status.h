#ifndef STATUS_H
#define STATUS_H

// SUCCESS — строка прочитана, INVALID_INPUT — некорректная строка, ERROR — конец ввода или ошибка

typedef enum Status {
    SUCCESS = 0,
    ERROR = 1,
    INVALID_INPUT = 2
} Status;

#endif
