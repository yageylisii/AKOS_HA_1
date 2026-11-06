#define _POSIX_C_SOURCE 200809L

#include "ipc/shared_memory.h"
#include "asserts.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// Общая часть: отображаем уже открытый дескриптор сегмента в адресное
// пространство процесса и запоминаем параметры для последующей очистки.
static Status map_segment(SharedMemory *memory, int descriptor, const char *name,
                          size_t size, bool owner)
{
    void *address = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
    // Дескриптор больше не нужен: отображение существует независимо от него.
    close(descriptor);
    if (address == MAP_FAILED) {
        perror("mmap");
        if (owner) {
            shm_unlink(name);
        }
        return ERROR;
    }

    memory->address = address;
    memory->size = size;
    memory->owner = owner;
    snprintf(memory->name, sizeof(memory->name), "%s", name);
    return SUCCESS;
}

Status shared_memory_create(SharedMemory *memory, const char *name, size_t size)
{
    SOFT_ASSERT(memory != NULL && name != NULL && size > 0,
                "Неверные параметры разделяемой памяти", ERROR);

    *memory = (SharedMemory){0};

    // O_EXCL показывает, что сегмент уже существует. Это либо мусор после
    // аварийного завершения прошлого запуска, либо работающая копия программы.
    int descriptor = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (descriptor == -1 && errno == EEXIST) {
        fprintf(stderr, "Сегмент %s уже существует, удаляю остаток прошлого запуска\n", name);
        if (shm_unlink(name) == -1) {
            perror("shm_unlink");
            return ERROR;
        }
        descriptor = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    }
    if (descriptor == -1) {
        perror("shm_open");
        return ERROR;
    }

    // Задаём размер сегмента: без ftruncate обращение к памяти вызовет SIGBUS.
    if (ftruncate(descriptor, (off_t)size) == -1) {
        perror("ftruncate");
        close(descriptor);
        shm_unlink(name);
        return ERROR;
    }

    return map_segment(memory, descriptor, name, size, true);
}

Status shared_memory_attach(SharedMemory *memory, const char *name, size_t size)
{
    SOFT_ASSERT(memory != NULL && name != NULL && size > 0,
                "Неверные параметры разделяемой памяти", ERROR);

    *memory = (SharedMemory){0};

    int descriptor = shm_open(name, O_RDWR, 0600);
    if (descriptor == -1) {
        // ENOENT значит, что сегмент ещё не создан. Для участника,
        // запущенного раньше управляющей программы, это нормально:
        // о повторных попытках сообщает вызывающий код.
        if (errno != ENOENT) {
            perror("shm_open");
        }
        return ERROR;
    }

    // Проверяем, что подключаемся к сегменту ожидаемого размера.
    struct stat info;
    if (fstat(descriptor, &info) == -1) {
        perror("fstat");
        close(descriptor);
        return ERROR;
    }
    if ((size_t)info.st_size < size) {
        fprintf(stderr, "Размер сегмента %s меньше ожидаемого\n", name);
        close(descriptor);
        return ERROR;
    }

    return map_segment(memory, descriptor, name, size, false);
}

void shared_memory_detach(SharedMemory *memory)
{
    if (memory == NULL || memory->address == NULL) {
        return;
    }
    if (munmap(memory->address, memory->size) == -1) {
        perror("munmap");
    }
    memory->address = NULL;
}

void shared_memory_destroy(SharedMemory *memory)
{
    if (memory == NULL) {
        return;
    }
    bool owner = memory->owner;
    char name[sizeof(memory->name)];
    snprintf(name, sizeof(name), "%s", memory->name);

    shared_memory_detach(memory);

    // Имя удаляет только создатель сегмента: после shm_unlink новое
    // подключение по этому имени невозможно, а уже отображённые копии живут.
    if (owner && name[0] != '\0') {
        if (shm_unlink(name) == -1 && errno != ENOENT) {
            perror("shm_unlink");
        }
    }
    *memory = (SharedMemory){0};
}
