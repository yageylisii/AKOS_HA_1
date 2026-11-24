#include "components/model.h"

#include <stdint.h>
#include <string.h>

/* Схема версии 2: целые big-endian, bool как 0/1, double IEEE 754 binary64.
   Паддинг структур и размер size_t в сетевое сообщение не попадают.
   Один обход полей используется и для записи, и для чтения. */
_Static_assert(sizeof(double) == 8, "Protocol requires binary64 double");

typedef struct Wire {
    unsigned char *output;
    const unsigned char *input;
    size_t position;
    size_t capacity;
    bool reading;
    bool failed;
} Wire;

static void bytes(Wire *wire, void *value, size_t length)
{
    if (length > wire->capacity - wire->position) {
        wire->failed = true;
        return;
    }
    if (wire->reading) {
        memcpy(value, wire->input + wire->position, length);
    } else if (wire->output != NULL) {
        memcpy(wire->output + wire->position, value, length);
    }
    wire->position += length;
}

static void number(Wire *wire, uint64_t *value, size_t width)
{
    unsigned char buffer[8] = {0};
    if (!wire->reading) {
        for (size_t i = 0; i < width; ++i) {
            buffer[width - i - 1] = (unsigned char)(*value >> (i * 8));
        }
    }
    bytes(wire, buffer, width);
    if (wire->reading && !wire->failed) {
        *value = 0;
        for (size_t i = 0; i < width; ++i) {
            *value = (*value << 8) | buffer[i];
        }
    }
}

static void integer(Wire *wire, int *value)
{
    uint64_t n = (uint32_t)*value;
    number(wire, &n, 4);
    if (wire->reading) {
        *value = n <= INT32_MAX ? (int)n : -1 - (int)(UINT32_MAX - n);
    }
}

static void unsigned_integer(Wire *wire, unsigned int *value)
{
    uint64_t n = *value;
    number(wire, &n, 4);
    if (wire->reading) {
        *value = (unsigned int)n;
    }
}

static void boolean(Wire *wire, bool *value)
{
    uint64_t n = *value ? 1 : 0;
    number(wire, &n, 4);
    if (wire->reading) {
        if (n > 1) {
            wire->failed = true;
        }
        *value = n != 0;
    }
}

static void real(Wire *wire, double *value)
{
    uint64_t n = 0;
    memcpy(&n, value, 8);
    number(wire, &n, 8);
    if (wire->reading) {
        memcpy(value, &n, 8);
    }
}

static void queue(Wire *wire, IntQueue *q)
{
    integer(wire, &q->head);
    integer(wire, &q->count);
    for (int i = 0; i < QUEUE_CAPACITY; ++i) {
        integer(wire, &q->items[i]);
    }
}

static void fields(Wire *wire, Model *model)
{
#define I(field) integer(wire, &model->field)
#define B(field) boolean(wire, &model->field)
#define U(field) unsigned_integer(wire, &model->field)
    I(settings.cabines); I(settings.capacity); I(settings.max_passengers);
    I(settings.work_time); I(settings.time_between_stations);
    I(settings.breakdown_duration); I(settings.arrival_interval);
    I(settings.passengers_per_interval); I(settings.boarding_time); I(settings.unloading_time);
    real(wire, &model->settings.vip_probability);
    real(wire, &model->settings.breakdown_probability);
    I(settings.wind_limit); I(settings.wind_count);
    for (int i = 0; i < MAX_WIND_INTERVALS; ++i) {
        I(settings.wind[i].start); I(settings.wind[i].end); I(settings.wind[i].speed);
    }
    I(settings.display_delay_ms); U(settings.seed);
    for (int i = 0; i < STATION_COUNT; ++i) {
        I(stations[i].id); U(stations[i].rng);
        queue(wire, &model->stations[i].vip);
        queue(wire, &model->stations[i].regular);
        queue(wire, &model->stations[i].waiting_cabins);
        queue(wire, &model->stations[i].cabins);
    }
    for (int i = 0; i < MAX_CABINS; ++i) {
        I(cabins[i].id); I(cabins[i].state); I(cabins[i].passengers);
        for (int j = 0; j < MAX_CABIN_CAPACITY; ++j) {
            I(cabins[i].passenger_ids[j]);
        }
        I(cabins[i].boarding_passenger); I(cabins[i].operation_time_remaining);
        B(cabins[i].doors_open); B(cabins[i].departure_requested); B(cabins[i].departure_allowed);
        I(cabins[i].station_index); I(cabins[i].destination_index); B(cabins[i].on_platform);
    }
    for (int i = 0; i < MAX_PASSENGERS; ++i) {
        I(passengers[i].id); I(passengers[i].priority); I(passengers[i].state);
        I(passengers[i].origin); I(passengers[i].destination); I(passengers[i].cabin_index);
        I(passengers[i].arrival_time); I(passengers[i].finish_time);
    }
    I(statistics.accepted); I(statistics.delivered); I(statistics.evacuated);
    I(statistics.rejected); I(statistics.trips); I(statistics.breakdowns); I(statistics.wind_stops);
    uint64_t wait = (uint64_t)model->statistics.total_wait;
    number(wire, &wait, 8);
    if (wire->reading) {
        if (wait > INT64_MAX) { wire->failed = true; }
        model->statistics.total_wait = wait <= INT64_MAX ? (long long)wait : 0;
    }
    I(current_time); I(cabin_count); I(passengers_created); I(next_passenger_id); I(status);
    B(admission_closed); B(wind_stop); B(technical_stop); I(technical_stop_remaining);
    B(interrupted); B(invariant_failed); U(operator_rng);
    bytes(wire, model->invariant_message, sizeof(model->invariant_message));
    uint64_t length = model->log.length;
    number(wire, &length, 4);
    if (wire->reading) { model->log.length = (size_t)length; }
    I(log.event_time); B(log.has_events); B(log.printed_group); B(log.overflow);
#undef I
#undef B
#undef U
}

size_t model_wire_header_size(void)
{
    Model empty = {0};
    Wire wire = {.capacity = MODEL_WIRE_CAPACITY};
    fields(&wire, &empty);
    return wire.position;
}

size_t model_wire_size(const Model *model)
{
    return model != NULL && model->log.length <= LOG_BUFFER_SIZE
               ? model_wire_header_size() + model->log.length : 0;
}

Status model_to_wire(const Model *model, void *data, size_t capacity, size_t *length)
{
    if (model == NULL || data == NULL || length == NULL || model->log.length > LOG_BUFFER_SIZE) {
        return ERROR;
    }
    /* На записи обход полей не меняет модель; локальная копия сохраняет const-контракт. */
    Model copy = *model;
    Wire wire = {.output = data, .capacity = capacity};
    fields(&wire, &copy);
    bytes(&wire, copy.log.buffer, copy.log.length);
    *length = wire.position;
    return wire.failed ? ERROR : SUCCESS;
}

Status model_decode_wire(Model *model, const void *data, size_t length)
{
    if (model == NULL || data == NULL || length < model_wire_header_size() ||
        length > MODEL_WIRE_CAPACITY) {
        return ERROR;
    }
    memset(model, 0, sizeof(*model));
    Wire wire = {.input = data, .capacity = length, .reading = true};
    fields(&wire, model);
    if (wire.failed || model->log.length > LOG_BUFFER_SIZE ||
        model->log.length != length - wire.position) {
        return ERROR;
    }
    bytes(&wire, model->log.buffer, model->log.length);
    return wire.failed ? ERROR : SUCCESS;
}
