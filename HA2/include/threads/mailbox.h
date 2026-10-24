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

Status mailbox_init(Mailbox *mailbox);
Status mailbox_send(Mailbox *mailbox, ActorMessage message);
Status mailbox_receive(Mailbox *mailbox, ActorMessage *message);
void mailbox_close(Mailbox *mailbox);
void mailbox_destroy(Mailbox *mailbox);

#endif
