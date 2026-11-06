#define _POSIX_C_SOURCE 200809L

#include "roles/roles.h"
#include "components/queue.h"

// Семейство пассажиров станции. Отдельного процесса на каждого пассажира
// не заводим: по требованию задания сущности с поведением могут быть
// представлены семействами. Процесс станции отвечает за появление
// пассажиров и ведение их очередей, а решения о посадке принимает
// кабина, забирая людей из очередей под семафором SEM_MODEL.

// Группа пассажиров распределяется между станциями: чётные группы
// получают остаток на станцию A, нечётные — на станцию B.
static int station_quota(const SharedModel *model, int station_index)
{
    int total = model->settings.passengers_per_interval;
    int base = total / STATION_COUNT;
    int extra = total % STATION_COUNT;
    int group = model->settings.arrival_interval > 0
                    ? model->current_time / model->settings.arrival_interval
                    : 0;
    if (extra > 0 && group % STATION_COUNT == station_index) {
        base += extra;
    }
    return base;
}

void station_step(SharedModel *model, int station_index)
{
    if (model == NULL || station_index < 0 || station_index >= STATION_COUNT) {
        return;
    }
    if (model->status == SERVICE_FINISHED || model->status == SERVICE_EVACUATING) {
        return;
    }
    // После закрытия входа новые пассажиры не появляются.
    if (model->admission_closed) {
        return;
    }
    if (model->settings.arrival_interval <= 0 ||
        model->current_time % model->settings.arrival_interval != 0) {
        return;
    }

    Station *station = &model->stations[station_index];
    int quota = station_quota(model, station_index);

    for (int created = 0; created < quota; ++created) {
        // Общий предел пассажиров защищён тем же семафором, что и модель,
        // поэтому обе станции не могут превысить его одновременно.
        if (model->passengers_created >= model->settings.max_passengers) {
            break;
        }
        if (model->passengers_created >= MAX_PASSENGERS) {
            break;
        }

        int passenger_id = model->next_passenger_id++;
        ++model->passengers_created;

        Passenger *passenger = &model->passengers[passenger_id - 1];
        passenger->id = passenger_id;
        passenger->priority =
            model_random_unit(&station->rng) < model->settings.vip_probability
                ? PRIORITY_VIP
                : PRIORITY_REGULAR;
        passenger->state = PASSENGER_WAITING;
        passenger->origin = station_index;
        passenger->destination = station_index == 0 ? 1 : 0;
        passenger->cabin_index = -1;
        passenger->arrival_time = model->current_time;
        passenger->finish_time = -1;

        IntQueue *queue = passenger->priority == PRIORITY_VIP ? &station->vip
                                                              : &station->regular;
        if (!queue_push(queue, passenger_id)) {
            // Очередь переполнена: пассажир не принимается в систему.
            passenger->state = PASSENGER_NONE;
            --model->passengers_created;
            --model->next_passenger_id;
            break;
        }

        ++model->statistics.accepted;
        log_event(&model->log, model->current_time,
                  "🧍 Пассажир %d прибыл на станцию %s (%s), едет на станцию %s",
                  passenger->id, station_name(station_index),
                  passenger->priority == PRIORITY_VIP ? "VIP" : "обычный",
                  station_name(passenger->destination));
    }
}
