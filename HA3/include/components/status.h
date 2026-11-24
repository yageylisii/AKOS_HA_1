#ifndef STATUS_H
#define STATUS_H

// код SUCCESS — операция выполнена, INVALID_INPUT — некорректные данные,
// код ERROR — конец ввода, ошибка системного вызова или нарушение предусловия
typedef enum Status {
    SUCCESS = 0,
    ERROR = 1,
    INVALID_INPUT = 2
} Status;

#endif
