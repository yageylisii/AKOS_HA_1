#define _POSIX_C_SOURCE 200809L

#include "roles/roles.h"
#include "components/queue.h"

// оператор — самостоятельный участник модели. За один такт модельного
// времени он следит за погодой и неисправностями, закрывает вход
// и разрешает отправление кабин, у которых закрыты двери
// фаза оператора выполняется до фазы кабин и станций, поэтому решения
// об остановке действуют уже в этом такте

static void handle_wind(SharedModel *model)
{
    bool wind_now = model_in_wind_interval(model, model->current_time);
    if (wind_now == model->wind_stop) {
        return;
    }

    model->wind_stop = wind_now;
    if (wind_now) {
        ++model->statistics.wind_stops;
        log_event(&model->log, model->current_time,
                  "🛑 Оператор: сильный ветер, движение остановлено");
    } else {
        log_event(&model->log, model->current_time,
                  "✅ Оператор: ветер стих");
    }
}

static void handle_breakdown(SharedModel *model)
{
    if (model->technical_stop) {
        --model->technical_stop_remaining;
        if (model->technical_stop_remaining <= 0) {
            model->technical_stop = false;
            model->technical_stop_remaining = 0;
            log_event(&model->log, model->current_time,
                      "✅ Оператор: неисправность устранена");
        }
        return;
    }

    // вероятность поломки проверяется раз в модельную минуту
    if (model->current_time == 0 || model->current_time % 60 != 0 || model->wind_stop) {
        return;
    }
    // пока все операции простаивают, новые поломки не генерируются:
    // ломаться нечему, а журнал засорялся бы пустыми остановками
    if (!model_has_active_operations(model)) {
        return;
    }
    if (model_random_unit(&model->operator_rng) >= model->settings.breakdown_probability) {
        return;
    }

    model->technical_stop = true;
    model->technical_stop_remaining = model->settings.breakdown_duration;
    ++model->statistics.breakdowns;
    log_event(&model->log, model->current_time,
              "🛑 Оператор: техническая неисправность, остановка на %d с",
              model->settings.breakdown_duration);
}

static void handle_admission(SharedModel *model)
{
    if (model->admission_closed || model->current_time < model->settings.work_time) {
        return;
    }
    model->admission_closed = true;
    log_event(&model->log, model->current_time,
              "🛑 Оператор: вход закрыт, принятые пассажиры будут доставлены");
}

// отправление разрешается только после закрытия дверей и завершения
// всех операций на станции — это прямое требование условия задачи
static void grant_departures(SharedModel *model)
{
    if (!model_movement_allowed(model)) {
        return;
    }
    for (int index = 0; index < model->cabin_count; ++index) {
        Cabin *cabin = &model->cabins[index];
        if (cabin->state != ON_STATION || !cabin->departure_requested) {
            continue;
        }
        if (cabin->doors_open || cabin->departure_allowed) {
            continue;
        }
        if (cabin->operation_time_remaining != 0) {
            continue;
        }
        cabin->departure_allowed = true;
        log_event(&model->log, model->current_time,
                  "📣 Оператор разрешил отправление кабины %d со станции %s",
                  cabin->id, station_name(cabin->station_index));
    }
}

void operator_step(SharedModel *model)
{
    if (model == NULL || model->status == SERVICE_FINISHED) {
        return;
    }

    bool was_stopped = model_is_stopped(model);

    handle_wind(model);
    handle_breakdown(model);
    handle_admission(model);

    bool is_stopped = model_is_stopped(model);
    if (was_stopped && !is_stopped) {
        log_event(&model->log, model->current_time, "✅ Движение возобновлено");
    }
    model->status = is_stopped ? SERVICE_STOPPED : SERVICE_RUNNING;

    grant_departures(model);
}
