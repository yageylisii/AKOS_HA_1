#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "threads/runtime.h"

// пропускает задержку журнала при проверке настоящих потоков
int test_nanosleep(const struct timespec *requested, struct timespec *remaining);
// пропускает задержку журнала при проверке настоящих потоков
int test_nanosleep(const struct timespec *requested, struct timespec *remaining) {
    (void)requested;
    (void)remaining;
    return 0;
}

// создаёт небольшую модель с реальными потоками для всех участников
static void start_model(Service *service, int cabins, int passengers) {
    *service = (Service){0};
    service->settings = (Settings){.cabin_count = cabins, .capacity = 2,
        .travel_time = 3, .work_time = 15, .max_passengers = passengers,
        .arrival_interval = 2, .passengers_per_interval = 3,
        .boarding_time = 2, .unloading_time = 2,
        .vip_probability = .5, .breakdown_duration = 3};
    assert(service_init(service) == SUCCESS);
    service->runtime = calloc(1, sizeof(ThreadRuntime));
    assert(service->runtime != NULL);
    assert(runtime_init(service->runtime, service) == SUCCESS);
}

// проверяет выбор пассажира станцией и отказ пассажира от неверного перехода
static void test_replies(void) {
    Service service;
    start_model(&service, 1, 2);
    assert(runtime_start_passenger(service.runtime, 1) == SUCCESS);
    ActorMessage message = {.type = ACTOR_MESSAGE_PASSENGER_BOARDED, .cabin_id = 1};
    assert(actor_request(&service, PASSENGER_ACTOR, 1, &message) == ERROR);
    message = (ActorMessage){.type = STATION_JOIN, .entity_id = 1, .priority = 1};
    assert(actor_request(&service, STATION_ACTOR, 1, &message) == SUCCESS);
    message = (ActorMessage){.type = STATION_JOIN, .entity_id = 2, .priority = 0};
    assert(actor_request(&service, STATION_ACTOR, 1, &message) == SUCCESS);
    message = (ActorMessage){.type = STATION_SELECT};
    assert(actor_request(&service, STATION_ACTOR, 1, &message) == SUCCESS);
    assert(message.entity_id == 2 && message.priority == 0);
    message = (ActorMessage){.type = STATION_SELECT};
    assert(actor_request(&service, STATION_ACTOR, 1, &message) == SUCCESS);
    assert(message.entity_id == 1 && message.priority == 1);
    service_destroy(&service);
}

// проверяет завершение поездок и учёт людей при реальном обмене запросами
static void test_threaded_runs(void) {
    for (int seed = 0; seed < 12; ++seed) {
        srand((unsigned)seed);
        Service service;
        start_model(&service, seed + 1, 12);
        service.settings.wind_count = 1;
        service.settings.wind[0] = (BadWeather){5, 10};
        int steps = 0;
        while (service.status != SERVICE_FINISHED && steps++ < 500) {
            assert(service_step(&service) == SUCCESS);
            int active = 0;
            for (int index = 0; index < STATION_COUNT; ++index) {
                active += waiting_passengers(&service.stations[index]);
                assert(service.stations[index].cabins.count <= STATION_CABIN_CAPACITY);
            }
            for (int index = 0; index < service.settings.cabin_count; ++index) {
                Cabin *cabin = &service.cabins[index];
                int count = cabin->passengers + cabin->has_boarding_passenger;
                assert(count <= service.settings.capacity);
                active += count;
                if (cabin->state == IN_TRANSIT) assert(!cabin->doors_open);
            }
            assert(active + service.statistics.delivered == service.statistics.accepted);
        }
        assert(service.status == SERVICE_FINISHED);
        assert(service.statistics.accepted == 12 && service.statistics.delivered == 12);
        for (int index = 0; index < 12; ++index)
            assert(service.runtime->passengers[index].state == DELIVERED);
        service_destroy(&service);
    }
}

// проверяет эвакуацию ожидающих садящихся и едущих пассажиров
static void test_evacuation(void) {
    for (int stop_time = 1; stop_time < 12; ++stop_time) {
        Service service;
        start_model(&service, 2, 12);
        while (service.current_time < stop_time) assert(service_step(&service) == SUCCESS);
        assert(service_evacuate(&service) == SUCCESS);
        assert(service.statistics.accepted == service.statistics.delivered + service.statistics.evacuated);
        for (int index = 0; index < service.statistics.accepted; ++index) {
            PassengerState state = service.runtime->passengers[index].state;
            assert(state == DELIVERED || state == EVACUATED);
        }
        service_destroy(&service);
    }
}

// запускает проверки с настоящими pthread мьютексами и семафорами
int main(void) {
    test_replies();
    test_threaded_runs();
    test_evacuation();
    fprintf(stderr, "Worker tests passed\n");
    return 0;
}
