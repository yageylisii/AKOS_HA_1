#include "inputs/input_data.h"
#include "components/service.h"

#include <stdlib.h>
#include <signal.h>
#include <time.h>

static void request_shutdown(int signal_number) {
    (void)signal_number;
    service_interrupted = 1;
}

int main(void) {
    Service service = {0};
    
    if (input_data(&service.settings) != SUCCESS) {
        return ERROR;
    }

    struct sigaction action = {0};
    action.sa_handler = request_shutdown;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0) {
        return ERROR;
    }

    srand((unsigned)time(NULL)); // настраиваем генератор случайный чисел
    if (service_init(&service) != SUCCESS){
        service_destroy(&service);
        return ERROR;
    }
    Status result = service_run(&service);
    service_destroy(&service);
    return result;
}
