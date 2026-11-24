#ifndef IPC_NAMES_H
#define IPC_NAMES_H

// у каждого варианта программы свой сегмент разделяемой памяти,
// чтобы запущенные одновременно варианты не мешали друг другу
#define SHM_NAME_UNNAMED    "/akos46_cable_car_unnamed"
#define SHM_NAME_NAMED      "/akos46_cable_car_named"
#define SHM_NAME_STANDALONE "/akos46_cable_car_standalone"

// префикс имён семафоров: к нему добавляется номер слота, например /akos46_named_0
#define SEM_PREFIX_NAMED      "/akos46_named"
#define SEM_PREFIX_STANDALONE "/akos46_standalone"

#endif
