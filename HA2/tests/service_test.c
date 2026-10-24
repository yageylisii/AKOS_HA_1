#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "components/service.h"

// В тестах пропускаем только реальную задержку журнала, не модельное время.
int test_nanosleep(const struct timespec *requested, struct timespec *remaining);
int test_nanosleep(const struct timespec *requested, struct timespec *remaining) {
    (void)requested;
    (void)remaining;
    return 0;
}

static Settings settings(void) {
    return (Settings){.cabines=10,.capacity=20,.time_between_stations=30,.work_time=350,
        .max_passengers=100,.arrival_interval=50,.passengers_per_interval=20,
        .boarding_time=10,.unloading_time=10,.vip_probability=.5,
        .breakdown_probability=.5,.breakdown_duration=20};
}

static void check_invariants(const Service *service) {
    int cabin_locations[32]={0};
    int *passenger_locations=calloc(1001,sizeof(*passenger_locations));
    assert(passenger_locations != NULL);
    int active_passengers=0;
    for(int station_index=0;station_index<STATION_COUNT;++station_index) {
        const Station *station=&service->stations[station_index];
        assert(station->cabins.count<=STATION_CABIN_CAPACITY);
        const Queue *queues[]={&station->vip,&station->regular};
        for(int queue_index=0;queue_index<2;++queue_index) {
            int count=0;
            for(QueueNode *node=queues[queue_index]->head;node;node=node->next) {
                assert(++passenger_locations[node->passenger.id]==1);
                ++count;
            }
            assert(count==queues[queue_index]->count);
            active_passengers+=count;
        }
        int count=0;
        for(CabinQueueNode *node=station->cabins.head;node;node=node->next) {
            assert(++cabin_locations[node->cabin_id]==1);
            const Cabin *cabin=&service->cabins[node->cabin_id-1];
            assert(cabin->departure_station==station_index+1);
            assert(cabin->state==ON_STATION || cabin->state==DROP || cabin->state==BOARDING);
            assert(cabin->doors_open);
            ++count;
        }
        assert(count==station->cabins.count);
        count=0;
        for(CabinQueueNode *node=station->waiting_cabins.head;node;node=node->next) {
            assert(++cabin_locations[node->cabin_id]==1);
            const Cabin *cabin=&service->cabins[node->cabin_id-1];
            assert(cabin->destination_station==station_index+1);
            assert(cabin->state==WAITING_FOR_STATION && !cabin->doors_open);
            ++count;
        }
        assert(count==station->waiting_cabins.count);
    }
    for(int index=0;index<service->settings.cabines;++index) {
        const Cabin *cabin=&service->cabins[index];
        assert(cabin->passengers>=0 && cabin->passengers+cabin->has_boarding_passenger<=service->settings.capacity);
        if(cabin->state==PARKED || cabin->state==IN_WALK) {
            assert(!cabin->doors_open && cabin_locations[cabin->id]==0);
        } else assert(cabin_locations[cabin->id]==1);
        for(int passenger_index=0;passenger_index<cabin->passengers;++passenger_index)
            assert(++passenger_locations[cabin->passenger_list[passenger_index].id]==1);
        if(cabin->has_boarding_passenger) {
            assert(cabin->state==BOARDING);
            assert(++passenger_locations[cabin->boarding_passenger.id]==1);
        }
        active_passengers+=cabin->passengers+cabin->has_boarding_passenger;
    }
    assert(active_passengers+service->statistics.delivered+service->statistics.evacuated==service->statistics.accepted);
    free(passenger_locations);
}

static void step(Service *service) {
    assert(service_step(service)==SUCCESS);
    check_invariants(service);
}

static void add_person(Service *service,int station_id,int id,int priority) {
    Passenger passenger={.id=id,.priority=priority};
    Queue *queue=priority==0 ? &service->stations[station_id-1].vip : &service->stations[station_id-1].regular;
    assert(add_passenger(queue,passenger)==SUCCESS);
    ++service->statistics.accepted;
    service->next_passenger_id=id+1;
}

static void test_reserved_boarding(void) {
    Service service={0};service.settings=settings();
    service.settings.cabines=1;service.settings.capacity=2;service.settings.max_passengers=0;
    service.settings.breakdown_probability=0;
    assert(service_init(&service)==SUCCESS);
    add_person(&service,1,1,1);
    step(&service); // Начало посадки в t=0.
    while(service.current_time<9) step(&service);
    add_person(&service,1,2,0); // Новый VIP не прерывает уже начатую посадку.
    step(&service);
    assert(service.cabins[0].passengers==0);
    step(&service); // t=10
    assert(service.cabins[0].passengers==1 && service.cabins[0].passenger_list[0].id==1);
    assert(service.cabins[0].boarding_passenger.id==2);
    while(service.current_time<20) step(&service);
    assert(service.cabins[0].passengers==1);
    step(&service);
    assert(service.cabins[0].passengers==2 && service.cabins[0].state==IN_WALK);
    while(service.current_time<50) step(&service);
    step(&service); // Прибытие t=50; сразу началась высадка.
    assert(service.cabins[0].state==DROP && service.cabins[0].operation_time_remaining==10);
    while(service.current_time<60) step(&service);
    assert(service.statistics.delivered==0);
    step(&service);
    assert(service.statistics.delivered==1);
    service_destroy(&service);
}

static void test_parallel_and_capacity(void) {
    Service service={0};service.settings=settings();service.settings.cabines=4;
    service.settings.capacity=1;service.settings.max_passengers=0;service.settings.breakdown_probability=0;
    assert(service_init(&service)==SUCCESS);
    // Четыре полные кабины одновременно заканчивают путь на A.
    for(int index=0;index<4;++index) {
        Cabin *cabin=&service.cabins[index];
        assert(remove_cabin(&service.stations[cabin->departure_station-1].cabins,cabin->id)==SUCCESS);
        cabin->state=IN_WALK;cabin->doors_open=false;cabin->destination_station=1;
        cabin->operation_time_remaining=1;
        cabin->passenger_list[0]=(Passenger){.id=index+1};cabin->passengers=1;
    }
    service.statistics.accepted=4;service.current_time=1;
    step(&service);
    assert(service.stations[0].cabins.count==3 && service.stations[0].waiting_cabins.count==1);
    for(int index=0;index<3;++index) assert(service.cabins[index].state==DROP);
    while(service.current_time<11) step(&service);
    step(&service);
    assert(service.statistics.delivered==3);
    // Освободившееся место используется в t=11, первая высадка четвёртой — t=21.
    assert(service.cabins[3].state==DROP && service.cabins[3].operation_time_remaining==10);
    while(service.current_time<21) step(&service);
    step(&service);assert(service.statistics.delivered==4);
    service_destroy(&service);
}

static void test_stops_and_finish(void) {
    Service service={0};service.settings=settings();service.settings.cabines=1;
    service.settings.capacity=1;service.settings.max_passengers=0;service.settings.work_time=1;
    service.settings.breakdown_probability=0;
    service.settings.wind_count=1;service.settings.wind[0]=(BadWeather){5,15};
    assert(service_init(&service)==SUCCESS);add_person(&service,1,1,0);
    while(service.current_time<20) step(&service);
    assert(service.cabins[0].passengers==0);step(&service);
    assert(service.cabins[0].state==IN_WALK); // 10 секунд посадки + 10 секунд ветра.
    while(service.current_time<50) {assert(service.status!=SERVICE_FINISHED);step(&service);}
    step(&service);assert(service.cabins[0].state==DROP);
    while(service.current_time<60) step(&service);
    step(&service);assert(service.status==SERVICE_FINISHED && service.current_time==60);
    assert(service.cabins[0].state==PARKED);service_destroy(&service);
    // Пустой уже начатый рейс тоже должен закончиться.
    service=(Service){0};service.settings=settings();service.settings.cabines=1;
    service.settings.max_passengers=0;service.settings.work_time=1;service.settings.breakdown_probability=0;
    assert(service_init(&service)==SUCCESS);
    assert(remove_cabin(&service.stations[0].cabins,1)==SUCCESS);
    service.cabins[0].state=IN_WALK;
    service.cabins[0].doors_open=false;
    service.cabins[0].operation_time_remaining=30;
    while(service.current_time<30) {step(&service);assert(service.status!=SERVICE_FINISHED);}
    step(&service);assert(service.status==SERVICE_FINISHED && service.current_time==30);
    service_destroy(&service);
}

static void test_limits_and_idle(void) {
    Service service={0};service.settings=settings();service.settings.cabines=MAX_CABINS+1;
    assert(service_init(&service)==ERROR && service.cabins==NULL);
    service.settings=settings();service.settings.breakdown_probability=.6;
    assert(service_init(&service)==ERROR && service.cabins==NULL);
    service.settings=settings();service.settings.max_passengers=0;
    service.settings.work_time=100;service.settings.breakdown_probability=0;
    assert(service_init(&service)==SUCCESS);
    while(service.current_time<100) {
        step(&service);
        for(int index=0;index<service.settings.cabines;++index)
            assert(service.cabins[index].state!=IN_WALK);
    }
    step(&service);assert(service.status==SERVICE_FINISHED);service_destroy(&service);
    service=(Service){0};service.settings=settings();service.settings.max_passengers=0;
    assert(service_init(&service)==SUCCESS);add_person(&service,1,1,0);
    service.current_time=service.settings.work_time+MAX_DRAIN_TIME;
    assert(service_step(&service)==SUCCESS);
    assert(service.status==SERVICE_FINISHED && service.statistics.evacuated==1);
    service_destroy(&service);
}

static void test_reserve_local_demand(void) {
    Service service={0};service.settings=settings();service.settings.cabines=5;
    service.settings.max_passengers=0;service.settings.breakdown_probability=0;
    assert(service_init(&service)==SUCCESS);
    assert(remove_cabin(&service.stations[0].cabins,5)==SUCCESS);
    service.cabins[4].state=PARKED;service.cabins[4].doors_open=false;
    add_person(&service,1,1,1);add_person(&service,1,2,1);
    for(int id=3;id<=8;++id) add_person(&service,2,id,1);
    step(&service);
    // A: оба пассажира уже на посадке. Очередь на B не вызывает резерв на A.
    assert(service.stations[0].regular.count==0);
    assert(service.stations[1].regular.count>0);
    assert(service.cabins[4].state==PARKED);
    add_person(&service,1,9,1);step(&service);
    assert(service.cabins[4].state==BOARDING);
    assert(service.cabins[4].has_boarding_passenger && service.cabins[4].boarding_passenger.id==9);
    service_destroy(&service);

    service=(Service){0};service.settings=settings();service.settings.cabines=9;
    service.settings.max_passengers=0;service.settings.breakdown_probability=0;
    assert(service_init(&service)==SUCCESS);
    // На A одна работающая кабина и несколько резервных, но пассажиров лишь двое.
    for(int index=2;index<=4;index+=2) {
        assert(remove_cabin(&service.stations[0].cabins,index+1)==SUCCESS);
        service.cabins[index].state=PARKED;service.cabins[index].doors_open=false;
    }
    add_person(&service,1,1,1);add_person(&service,1,2,1);
    step(&service);
    assert(service.cabins[0].has_boarding_passenger);
    assert(service.cabins[2].has_boarding_passenger);
    assert(service.cabins[4].state==PARKED && service.cabins[6].state==PARKED);
    service_destroy(&service);
}

int main(void) {
    test_reserve_local_demand();
    test_limits_and_idle();
    test_reserved_boarding();test_parallel_and_capacity();test_stops_and_finish();
    for(int seed=0;seed<30;++seed) {
        srand((unsigned)seed);
        Service service={0};service.settings=settings();service.settings.cabines=seed%12+1;
        service.settings.wind_count=2;service.settings.wind[0]=(BadWeather){13,26};
        service.settings.wind[1]=(BadWeather){20,40};
        assert(service_init(&service)==SUCCESS);check_invariants(&service);
        int steps=0;
        while(service.status!=SERVICE_FINISHED && steps++<20000) step(&service);
        assert(service.status==SERVICE_FINISHED);
        assert(service.statistics.accepted==100 && service.statistics.delivered==100);
        for(int index=0;index<service.settings.cabines;++index) assert(service.cabins[index].state==PARKED);
        assert(log_flush(&service.event_log)==SUCCESS);service_destroy(&service);
    }
    Service service={0};service.settings=settings();service.settings.max_passengers=0;
    assert(service_init(&service)==SUCCESS);add_person(&service,1,1,0);step(&service);
    assert(service_evacuate(&service)==SUCCESS && service.statistics.evacuated==1);
    check_invariants(&service);service_destroy(&service);
    fprintf(stderr,"Service tests passed\n");
    return 0;
}
