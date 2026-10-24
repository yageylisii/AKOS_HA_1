#include "inputs/input_data.h"
#include "components/service.h"

#include <stdlib.h>
#include <time.h>

int main(void) {
    Service service = {0};
    
    if (input_data(&service.settings) != SUCCESS) {
        return ERROR;
    }
    srand((unsigned)time(NULL)); // настраиваем генератор случайный чисел
    if (service_init(&service) != SUCCESS){
        return ERROR;
    }
    Status result = service_run(&service);
    if (result != SUCCESS) {
        return ERROR;
    }

    service_destroy(&service);
    return result;
}
