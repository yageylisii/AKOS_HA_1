#include "inputs/input_data.h"
#include "components/service.h"

#include <stdlib.h>
#include <time.h>

int main(void) {
    Service service = {0};
    // По умолчанию сильного ветра нет: wind_count = 0.
    if (input_data(&service.settings) != SUCCESS) {
        return ERROR;
    }
    srand((unsigned)time(NULL)); // настраиваем генератор случайный чисел
    if (service_init(&service) != SUCCESS){
        return ERROR;
    }
    Status result = service_run(&service);
    service_destroy(&service);
    return result;
}
