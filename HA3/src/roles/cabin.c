#define _POSIX_C_SOURCE 200809L

#include "roles/roles.h"
#include "components/queue.h"

// Кабина — самостоятельный участник модели. За один такт она выполняет
// одну секунду своей текущей операции: высадки, посадки, движения
// или ожидания. Все переходы защищены семафором SEM_MODEL, поэтому
// состав кабины и очереди станции меняются согласованно.

static int other_station(int station_index)
{
    return station_index == 0 ? 1 : 0;
}

// Снимает кабину с места станции и освобождает его для следующей кабины.
static void leave_platform(SharedModel *model, Cabin *cabin, int cabin_index)
{
    if (!cabin->on_platform) {
        return;
    }
    queue_remove(&model->stations[cabin->station_index].cabins, cabin_index);
    cabin->on_platform = false;
}

// Допуск кабины на станцию: двери открываются, сначала идёт высадка.
static void admit_cabin(SharedModel *model, Cabin *cabin, int cabin_index, int station_index)
{
    Station *station = &model->stations[station_index];

    queue_push(&station->cabins, cabin_index);
    cabin->on_platform = true;
    cabin->station_index = station_index;
    cabin->destination_index = station_index;
    cabin->doors_open = true;
    cabin->departure_requested = false;
    cabin->departure_allowed = false;
    cabin->boarding_passenger = -1;

    log_event(&model->log, model->current_time,
              "✅ Кабина %d допущена на станцию %s, двери открыты",
              cabin->id, station_name(station_index));

    // Все пассажиры на борту ехали именно сюда, поэтому выходят все.
    if (cabin->passengers > 0) {
        cabin->state = DROP;
        cabin->operation_time_remaining = model->settings.unloading_time;
    } else {
        cabin->state = BOARDING;
        cabin->operation_time_remaining = 0;
    }
}

// Выбирает следующего пассажира: сначала VIP, затем обычная очередь.
// Внутри каждой категории сохраняется порядок прибытия.
static int take_next_passenger(SharedModel *model, int station_index)
{
    Station *station = &model->stations[station_index];
    int passenger_id = 0;

    if (queue_pop(&station->vip, &passenger_id)) {
        return passenger_id;
    }
    if (queue_pop(&station->regular, &passenger_id)) {
        return passenger_id;
    }
    return -1;
}

// Пустая кабина отправляется только за ожидающими пассажирами.
static bool cabin_should_depart(const SharedModel *model, const Cabin *cabin)
{
    if (cabin->passengers > 0) {
        return true;
    }
    return model_waiting_passengers(model, other_station(cabin->station_index)) > 0;
}

// Иначе кабина выводится на стоянку: после закрытия входа или
// чтобы освободить место прибывшей кабине.
static bool cabin_should_park(const SharedModel *model, const Cabin *cabin)
{
    if (cabin->passengers > 0) {
        return false;
    }
    bool nobody_waits = model_waiting_passengers(model, 0) == 0 &&
                        model_waiting_passengers(model, 1) == 0;
    if (model->admission_closed && nobody_waits) {
        return true;
    }
    return model->stations[cabin->station_index].waiting_cabins.count > 0;
}

// ---------------------------------------------------------------------------
// Обработчики отдельных состояний.
// ---------------------------------------------------------------------------

// Резерв и стоянка вне мест обслуживания.
static void step_parked(SharedModel *model, Cabin *cabin, int cabin_index)
{
    if (model->admission_closed &&
        model_waiting_passengers(model, 0) == 0 &&
        model_waiting_passengers(model, 1) == 0) {
        return; // Работа окончена, кабина остаётся на стоянке
    }

    bool needed = model_waiting_passengers(model, cabin->station_index) > 0 ||
                  model_waiting_passengers(model, other_station(cabin->station_index)) > 0;
    if (!needed) {
        return;
    }

    Station *station = &model->stations[cabin->station_index];
    // Прибывшие кабины получают освободившиеся места раньше кабин из резерва.
    if (station->waiting_cabins.count > 0 || station->cabins.count >= STATION_CABIN_CAPACITY) {
        return;
    }

    log_event(&model->log, model->current_time,
              "✅ Кабина %d подана из резерва на станции %s",
              cabin->id, station_name(cabin->station_index));
    admit_cabin(model, cabin, cabin_index, cabin->station_index);
}

// Кабина закончила путь и ждёт свободного места перед станцией.
static void step_waiting_for_station(SharedModel *model, Cabin *cabin, int cabin_index)
{
    Station *station = &model->stations[cabin->destination_index];
    if (station->cabins.count >= STATION_CABIN_CAPACITY) {
        return;
    }

    // Место занимает только голова очереди ожидающих кабин.
    int head = -1;
    if (!queue_peek(&station->waiting_cabins, &head) || head != cabin_index) {
        return;
    }
    queue_pop(&station->waiting_cabins, NULL);
    admit_cabin(model, cabin, cabin_index, cabin->destination_index);
}

// Высадка: по одному пассажиру за unloading_time секунд.
static void step_drop(SharedModel *model, Cabin *cabin)
{
    if (cabin->operation_time_remaining > 0) {
        --cabin->operation_time_remaining;
    }
    if (cabin->operation_time_remaining > 0) {
        return;
    }

    int passenger_id = cabin->passenger_ids[0];
    for (int seat = 0; seat + 1 < cabin->passengers; ++seat) {
        cabin->passenger_ids[seat] = cabin->passenger_ids[seat + 1];
    }
    --cabin->passengers;

    Passenger *passenger = &model->passengers[passenger_id - 1];
    passenger->state = PASSENGER_DELIVERED;
    passenger->cabin_index = -1;
    passenger->finish_time = model->current_time;
    ++model->statistics.delivered;
    model->statistics.total_wait += passenger->finish_time - passenger->arrival_time;

    log_event(&model->log, model->current_time,
              "📤 Пассажир %d вышел из кабины %d и доставлен на станцию %s",
              passenger->id, cabin->id, station_name(cabin->station_index));

    if (cabin->passengers > 0) {
        cabin->operation_time_remaining = model->settings.unloading_time;
        return;
    }
    // Высадка завершена, начинается посадка ожидающих.
    cabin->state = BOARDING;
    cabin->operation_time_remaining = 0;
}

// Закрытие дверей и запрос разрешения у оператора.
static void close_doors_and_request(SharedModel *model, Cabin *cabin, int cabin_index)
{
    Station *station = &model->stations[cabin->station_index];
    int waiting = station->vip.count + station->regular.count;

    if (waiting > 0 && cabin->passengers >= model->settings.capacity) {
        model->statistics.rejected += waiting;
        log_event(&model->log, model->current_time,
                  "🛑 Кабина %d заполнена (%d из %d), %d пассажиров ждут следующую",
                  cabin->id, cabin->passengers, model->settings.capacity, waiting);
    }

    cabin->doors_open = false;
    cabin->state = ON_STATION;
    cabin->operation_time_remaining = 0;
    cabin->departure_requested = true;
    cabin->departure_allowed = false;

    log_event(&model->log, model->current_time,
              "🟦 Кабина %d закрыла двери на станции %s, пассажиров %d",
              cabin->id, station_name(cabin->station_index), cabin->passengers);
    (void)cabin_index;
}

// Посадка: пассажир закрепляется за кабиной в начале операции.
static void step_boarding(SharedModel *model, Cabin *cabin, int cabin_index)
{
    // Завершение посадки ранее закреплённого пассажира.
    if (cabin->boarding_passenger > 0) {
        if (cabin->operation_time_remaining > 0) {
            --cabin->operation_time_remaining;
        }
        if (cabin->operation_time_remaining > 0) {
            return;
        }

        int passenger_id = cabin->boarding_passenger;
        Passenger *passenger = &model->passengers[passenger_id - 1];
        cabin->passenger_ids[cabin->passengers] = passenger_id;
        ++cabin->passengers;
        passenger->state = PASSENGER_RIDING;
        passenger->cabin_index = cabin_index;
        cabin->boarding_passenger = -1;

        log_event(&model->log, model->current_time,
                  "📥 Пассажир %d сел в кабину %d (занято %d из %d)",
                  passenger->id, cabin->id, cabin->passengers, model->settings.capacity);
        // Следующая операция начинается сразу, без скрытой секунды.
    }

    // Закрепляем следующего пассажира, если есть место и очередь не пуста.
    if (cabin->passengers < model->settings.capacity) {
        int passenger_id = take_next_passenger(model, cabin->station_index);
        if (passenger_id > 0) {
            Passenger *passenger = &model->passengers[passenger_id - 1];
            passenger->state = PASSENGER_ENTERING;
            passenger->cabin_index = cabin_index;
            cabin->boarding_passenger = passenger_id;
            cabin->operation_time_remaining = model->settings.boarding_time;

            log_event(&model->log, model->current_time,
                      "📥 Пассажир %d начал посадку в кабину %d на станции %s (%s)",
                      passenger->id, cabin->id, station_name(cabin->station_index),
                      passenger->priority == PRIORITY_VIP ? "VIP" : "обычный");
            return;
        }
    }

    // Больше сажать некого или мест нет.
    if (cabin_should_depart(model, cabin)) {
        close_doors_and_request(model, cabin, cabin_index);
        return;
    }
    if (cabin_should_park(model, cabin)) {
        cabin->doors_open = false;
        leave_platform(model, cabin, cabin_index);
        cabin->state = PARKED;
        cabin->operation_time_remaining = 0;
        log_event(&model->log, model->current_time,
                  "🅿️ Кабина %d выведена на стоянку у станции %s",
                  cabin->id, station_name(cabin->station_index));
    }
    // Иначе кабина ждёт пассажиров на станции с открытыми дверями.
}

// Кабина стоит с закрытыми дверями и ждёт разрешения оператора.
static void step_on_station(SharedModel *model, Cabin *cabin, int cabin_index)
{
    if (!cabin->departure_allowed) {
        return;
    }
    if (cabin->doors_open) {
        return; // Инвариант: отправление только при закрытых дверях
    }

    leave_platform(model, cabin, cabin_index);
    cabin->destination_index = other_station(cabin->station_index);
    cabin->state = IN_WALK;
    cabin->operation_time_remaining = model->settings.time_between_stations;
    cabin->departure_requested = false;
    cabin->departure_allowed = false;
    ++model->statistics.trips;

    log_event(&model->log, model->current_time,
              "🚠 Кабина %d отправилась со станции %s на станцию %s, пассажиров %d",
              cabin->id, station_name(cabin->station_index),
              station_name(cabin->destination_index), cabin->passengers);
}

// Движение между станциями.
static void step_in_walk(SharedModel *model, Cabin *cabin, int cabin_index)
{
    if (cabin->operation_time_remaining > 0) {
        --cabin->operation_time_remaining;
    }
    if (cabin->operation_time_remaining > 0) {
        return;
    }

    log_event(&model->log, model->current_time,
              "🚠 Кабина %d завершила путь и подошла к станции %s",
              cabin->id, station_name(cabin->destination_index));

    Station *station = &model->stations[cabin->destination_index];
    if (station->cabins.count < STATION_CABIN_CAPACITY && station->waiting_cabins.count == 0) {
        admit_cabin(model, cabin, cabin_index, cabin->destination_index);
        return;
    }

    cabin->state = WAITING_FOR_STATION;
    queue_push(&station->waiting_cabins, cabin_index);
    log_event(&model->log, model->current_time,
              "⏳ Кабина %d ожидает перед станцией %s: все места заняты",
              cabin->id, station_name(cabin->destination_index));
}

void cabin_step(SharedModel *model, int cabin_index)
{
    if (model == NULL || cabin_index < 0 || cabin_index >= model->cabin_count) {
        return;
    }
    if (model->status == SERVICE_FINISHED || model->status == SERVICE_EVACUATING) {
        return;
    }
    // При остановке таймеры кабин заморожены и состав кабин не меняется.
    if (model_is_stopped(model)) {
        return;
    }

    Cabin *cabin = &model->cabins[cabin_index];
    switch (cabin->state) {
        case PARKED:
            step_parked(model, cabin, cabin_index);
            break;
        case WAITING_FOR_STATION:
            step_waiting_for_station(model, cabin, cabin_index);
            break;
        case DROP:
            step_drop(model, cabin);
            break;
        case BOARDING:
            step_boarding(model, cabin, cabin_index);
            break;
        case ON_STATION:
            step_on_station(model, cabin, cabin_index);
            break;
        case IN_WALK:
            step_in_walk(model, cabin, cabin_index);
            break;
        default:
            break;
    }
}
