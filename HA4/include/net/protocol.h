#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "components/model.h"
#include "components/status.h"

/*
протокол прикладного уровня поверх TCP. TCP передаёт поток байт без границ
сообщений, поэтому каждое сообщение — кадр: заголовок фиксированной длины
и тело указанного в нём размера. Числа заголовка в сетевом порядке байт
*/

enum {
    PROTOCOL_MAGIC = 0x41433436, // "AC46": защита от подключения чужой программы
    PROTOCOL_VERSION = 2
};

typedef enum MessageType {
    MSG_HELLO = 1,   // клиент - сервер: роль и желаемый номер
    MSG_WELCOME,     // сервер - клиент: регистрация принята, выдан номер
    MSG_REJECT,      // сервер - клиент: регистрация отклонена, текст причины
    MSG_STEP,        // сервер - участник: разрешение такта и копия модели
    MSG_DONE,        // участник - сервер: изменённая модель и события такта
    MSG_EVENTS,      // сервер - наблюдатель: группа событий одной секунды
    MSG_STATE,       // сервер - наблюдатель: снимок модели для сводной таблицы
    MSG_NOTICE,      // сервер - клиент: служебное сообщение (пауза, подключения)
    MSG_SHUTDOWN     // сервер - клиент: работа завершена, текст итогов
} MessageType;

typedef enum Role {
    ROLE_OPERATOR = 0,
    ROLE_STATION,
    ROLE_CABIN,
    ROLE_OBSERVER,
    ROLE_COUNT
} Role;

typedef struct FrameHeader {
    uint32_t magic;
    uint32_t type;
    uint32_t length; // длина тела в байтах
} FrameHeader;

// тело сообщения MSG_HELLO
typedef struct HelloBody {
    uint32_t version;    // версия протокола
    uint32_t model_size; // размер фиксированной части сетевой модели
    int32_t role;        // тип Role
    int32_t index;       // желаемый номер станции или кабины, -1 — любой свободный
} HelloBody;

// тело сообщения MSG_WELCOME
typedef struct WelcomeBody {
    int32_t role;
    int32_t index;       // выданный номер: станция 0/1, кабина 0..N-1
    int32_t cabin_count;
    uint32_t seed;
} WelcomeBody;

enum { FRAME_HEADER_SIZE = 3 * sizeof(uint32_t) };

// наибольшее тело: модель с полностью заполненным журналом
size_t frame_max_payload(void);

// отправка одного кадра: заголовок и тело
Status frame_send(int fd, uint32_t type, const void *payload, size_t length);
Status frame_send_model(int fd, uint32_t type, const Model *model);
Status frame_send_text(int fd, uint32_t type, const char *text);

/*
блокирующий приём кадра для клиентов. Тело читается в buffer, при нехватке
места возвращается ERROR. closed = true, если сервер закрыл соединение
*/
Status frame_recv(int fd, uint32_t *type, void *buffer, size_t capacity,
                  size_t *length, bool *closed);

/*
накопитель кадров для сервера. Сервер работает на select() и не может
ждать, пока придёт всё сообщение, поэтому читает то, что уже доступно,
и вынимает кадры по мере их полного поступления
*/
typedef struct FrameReader {
    unsigned char *data;
    size_t used;
    size_t capacity;
} FrameReader;

Status frame_reader_init(FrameReader *reader);
void frame_reader_free(FrameReader *reader);

// один recv: closed = true при закрытии соединения клиентом
Status frame_reader_fill(FrameReader *reader, int fd, bool *closed);

/*
извлекает очередной полный кадр. has_frame = false, если кадр ещё не пришёл
целиком. Тело копируется в payload, ERROR при нарушении протокола
*/
Status frame_reader_next(FrameReader *reader, uint32_t *type, void *payload,
                         size_t capacity, size_t *length, bool *has_frame);

// заполнение и проверка тел служебных сообщений
HelloBody hello_make(int role, int index);
Status hello_parse(const void *payload, size_t length, int *role, int *index);
WelcomeBody welcome_make(int role, int index, int cabin_count, unsigned int seed);
Status welcome_parse(const void *payload, size_t length, WelcomeBody *body);

// подпись участника для вывода: «оператор», «станция A», «кабина 3»
void role_title(int role, int index, char *buffer, size_t capacity);

#endif
