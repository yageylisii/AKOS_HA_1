#define _POSIX_C_SOURCE 200809L

#include "net/protocol.h"
#include "asserts.h"
#include "inputs/input_logs.h"
#include "net/tcp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

size_t frame_max_payload(void)
{
    // текстовые сообщения (итоги, события) заведомо меньше модели
    return MODEL_WIRE_CAPACITY;
}

// заголовок раскладывается по байтам явно, без зависимости от выравнивания
static void header_encode(unsigned char *out, uint32_t type, uint32_t length)
{
    uint32_t fields[3] = {htonl(PROTOCOL_MAGIC), htonl(type), htonl(length)};
    memcpy(out, fields, FRAME_HEADER_SIZE);
}

static Status header_decode(const unsigned char *in, uint32_t *type, uint32_t *length)
{
    uint32_t fields[3];
    memcpy(fields, in, FRAME_HEADER_SIZE);
    SOFT_ASSERT(ntohl(fields[0]) == PROTOCOL_MAGIC, "Чужие данные в соединении", ERROR);
    *type = ntohl(fields[1]);
    *length = ntohl(fields[2]);
    SOFT_ASSERT(*length <= frame_max_payload(), "Слишком длинное сообщение", ERROR);
    return SUCCESS;
}

Status frame_send(int fd, uint32_t type, const void *payload, size_t length)
{
    SOFT_ASSERT(length <= frame_max_payload(), "Слишком длинное сообщение", ERROR);
    SOFT_ASSERT(length == 0 || payload != NULL, "Не передано тело сообщения", ERROR);

    unsigned char header[FRAME_HEADER_SIZE];
    header_encode(header, type, (uint32_t)length);
    TRY(tcp_send_all(fd, header, sizeof(header)));
    if (length > 0) {
        TRY(tcp_send_all(fd, payload, length));
    }
    return SUCCESS;
}

Status frame_send_model(int fd, uint32_t type, const Model *model)
{
    unsigned char payload[MODEL_WIRE_CAPACITY];
    size_t length = 0;
    TRY(model_to_wire(model, payload, sizeof(payload), &length));
    return frame_send(fd, type, payload, length);
}

Status frame_send_text(int fd, uint32_t type, const char *text)
{
    SOFT_ASSERT(text != NULL, "Не передан текст", ERROR);
    return frame_send(fd, type, text, strlen(text));
}

Status frame_recv(int fd, uint32_t *type, void *buffer, size_t capacity,
                  size_t *length, bool *closed)
{
    SOFT_ASSERT(type != NULL && length != NULL && buffer != NULL, "Неверные параметры", ERROR);

    unsigned char header[FRAME_HEADER_SIZE];
    TRY(tcp_recv_all(fd, header, sizeof(header), closed));

    uint32_t body_length = 0;
    TRY(header_decode(header, type, &body_length));
    SOFT_ASSERT(body_length <= capacity, "Сообщение не помещается в буфер", ERROR);

    if (body_length > 0) {
        TRY(tcp_recv_all(fd, buffer, body_length, closed));
    }
    *length = body_length;
    return SUCCESS;
}

Status frame_reader_init(FrameReader *reader)
{
    SOFT_ASSERT(reader != NULL, "Не передан накопитель", ERROR);
    reader->capacity = FRAME_HEADER_SIZE + frame_max_payload();
    reader->used = 0;
    reader->data = malloc(reader->capacity);
    SOFT_ASSERT(reader->data != NULL, "Не хватает памяти под буфер клиента", ERROR);
    return SUCCESS;
}

void frame_reader_free(FrameReader *reader)
{
    if (reader == NULL) {
        return;
    }
    free(reader->data);
    reader->data = NULL;
    reader->used = 0;
    reader->capacity = 0;
}

Status frame_reader_fill(FrameReader *reader, int fd, bool *closed)
{
    SOFT_ASSERT(reader != NULL && closed != NULL, "Неверные параметры", ERROR);
    *closed = false;
    SOFT_ASSERT(reader->used < reader->capacity, "Буфер клиента переполнен", ERROR);

    ssize_t received;
    do {
        received = recv(fd, reader->data + reader->used, reader->capacity - reader->used, 0);
    } while (received < 0 && errno == EINTR);

    if (received == 0) {
        *closed = true;
        return SUCCESS;
    }
    if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return SUCCESS;
    }
    if (received < 0) {
        // сброс соединения клиентом считаем обычным отключением
        *closed = true;
        return SUCCESS;
    }
    reader->used += (size_t)received;
    return SUCCESS;
}

Status frame_reader_next(FrameReader *reader, uint32_t *type, void *payload,
                         size_t capacity, size_t *length, bool *has_frame)
{
    SOFT_ASSERT(reader != NULL && type != NULL && length != NULL && has_frame != NULL,
                "Неверные параметры", ERROR);
    *has_frame = false;
    if (reader->used < FRAME_HEADER_SIZE) {
        return SUCCESS;
    }

    uint32_t body_length = 0;
    TRY(header_decode(reader->data, type, &body_length));
    size_t total = FRAME_HEADER_SIZE + body_length;
    if (reader->used < total) {
        return SUCCESS; // остаток кадра ещё в пути
    }
    SOFT_ASSERT(body_length <= capacity, "Сообщение не помещается в буфер", ERROR);

    if (body_length > 0) {
        memcpy(payload, reader->data + FRAME_HEADER_SIZE, body_length);
    }
    *length = body_length;
    *has_frame = true;

    // хвост, принадлежащий следующему кадру, сдвигаем в начало буфера
    memmove(reader->data, reader->data + total, reader->used - total);
    reader->used -= total;
    return SUCCESS;
}

HelloBody hello_make(int role, int index)
{
    HelloBody body;
    body.version = htonl(PROTOCOL_VERSION);
    body.model_size = htonl((uint32_t)model_wire_header_size());
    body.role = (int32_t)htonl((uint32_t)role);
    body.index = (int32_t)htonl((uint32_t)index);
    return body;
}

Status hello_parse(const void *payload, size_t length, int *role, int *index)
{
    SOFT_ASSERT(payload != NULL && role != NULL && index != NULL, "Неверные параметры", ERROR);
    SOFT_ASSERT(length == sizeof(HelloBody), "Неверный размер приветствия", ERROR);

    HelloBody body;
    memcpy(&body, payload, sizeof(body));
    SOFT_ASSERT(ntohl(body.version) == PROTOCOL_VERSION, "Другая версия протокола", ERROR);
    // Версия протокола и размер схемы должны совпадать.
    SOFT_ASSERT(ntohl(body.model_size) == model_wire_header_size(),
                "Клиент собран из других исходников: размер модели не совпадает", ERROR);
    *role = (int)(int32_t)ntohl((uint32_t)body.role);
    *index = (int)(int32_t)ntohl((uint32_t)body.index);
    SOFT_ASSERT(*role >= 0 && *role < ROLE_COUNT, "Неизвестная роль клиента", ERROR);
    return SUCCESS;
}

WelcomeBody welcome_make(int role, int index, int cabin_count, unsigned int seed)
{
    WelcomeBody body;
    body.role = (int32_t)htonl((uint32_t)role);
    body.index = (int32_t)htonl((uint32_t)index);
    body.cabin_count = (int32_t)htonl((uint32_t)cabin_count);
    body.seed = htonl(seed);
    return body;
}

Status welcome_parse(const void *payload, size_t length, WelcomeBody *body)
{
    SOFT_ASSERT(payload != NULL && body != NULL, "Неверные параметры", ERROR);
    SOFT_ASSERT(length == sizeof(WelcomeBody), "Неверный размер ответа сервера", ERROR);
    WelcomeBody raw;
    memcpy(&raw, payload, sizeof(raw));
    body->role = (int32_t)ntohl((uint32_t)raw.role);
    body->index = (int32_t)ntohl((uint32_t)raw.index);
    body->cabin_count = (int32_t)ntohl((uint32_t)raw.cabin_count);
    body->seed = ntohl(raw.seed);
    return SUCCESS;
}

void role_title(int role, int index, char *buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0) {
        return;
    }
    switch (role) {
        case ROLE_OPERATOR:
            snprintf(buffer, capacity, "оператор");
            break;
        case ROLE_STATION:
            snprintf(buffer, capacity, "станция %s", station_name(index));
            break;
        case ROLE_CABIN:
            snprintf(buffer, capacity, "кабина %d", index + 1);
            break;
        case ROLE_OBSERVER:
            snprintf(buffer, capacity, "наблюдатель %d", index + 1);
            break;
        default:
            snprintf(buffer, capacity, "неизвестный клиент");
            break;
    }
}
