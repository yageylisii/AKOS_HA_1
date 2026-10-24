#ifndef THREADS_MAILBOX_H
#define THREADS_MAILBOX_H

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

#include "components/status.h"
#include "threads/message.h"

typedef struct MailboxNode MailboxNode;

typedef struct Mailbox {
    pthread_mutex_t mutex;
    sem_t available;
    MailboxNode *head;
    MailboxNode *tail;
    bool closed;
    bool initialized;
} Mailbox;

// создаёт очередь сообщений мьютекс и семафор
Status mailbox_init(Mailbox *mailbox);
// добавляет сообщение и будит получателя
Status mailbox_send(Mailbox *mailbox, ActorMessage message);
// ожидает сообщение и извлекает его из очереди
Status mailbox_receive(Mailbox *mailbox, ActorMessage *message);
// закрывает очередь и будит ожидающего получателя
void mailbox_close(Mailbox *mailbox);
// освобождает очередь после завершения её потока
void mailbox_destroy(Mailbox *mailbox);

#endif
