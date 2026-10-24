#include "threads/mailbox.h"

#include <errno.h>
#include <stdlib.h>

struct MailboxNode {
    ActorMessage message;
    MailboxNode *next;
};

Status mailbox_init(Mailbox *mailbox) {
    if (mailbox == NULL) return ERROR;
    *mailbox = (Mailbox){0};
    if (pthread_mutex_init(&mailbox->mutex, NULL) != 0) return ERROR;
    if (sem_init(&mailbox->available, 0, 0) != 0) {
        pthread_mutex_destroy(&mailbox->mutex);
        return ERROR;
    }
    mailbox->initialized = true;
    return SUCCESS;
}

Status mailbox_send(Mailbox *mailbox, ActorMessage message) {
    if (mailbox == NULL || !mailbox->initialized) return ERROR;
    MailboxNode *node = malloc(sizeof(*node));
    if (node == NULL) return ERROR;
    node->message = message;
    node->next = NULL;

    if (pthread_mutex_lock(&mailbox->mutex) != 0) {
        free(node);
        return ERROR;
    }
    if (mailbox->closed) {
        pthread_mutex_unlock(&mailbox->mutex);
        free(node);
        return ERROR;
    }
    if (mailbox->tail != NULL) mailbox->tail->next = node;
    else mailbox->head = node;
    mailbox->tail = node;
    pthread_mutex_unlock(&mailbox->mutex);

    if (sem_post(&mailbox->available) != 0) return ERROR;
    return SUCCESS;
}

Status mailbox_receive(Mailbox *mailbox, ActorMessage *message) {
    if (mailbox == NULL || message == NULL || !mailbox->initialized) return ERROR;
    int wait_result;
    do {
        wait_result = sem_wait(&mailbox->available);
    } while (wait_result != 0 && errno == EINTR);
    if (wait_result != 0) return ERROR;

    if (pthread_mutex_lock(&mailbox->mutex) != 0) return ERROR;
    MailboxNode *node = mailbox->head;
    if (node == NULL) {
        pthread_mutex_unlock(&mailbox->mutex);
        return ERROR;
    }
    mailbox->head = node->next;
    if (mailbox->head == NULL) mailbox->tail = NULL;
    *message = node->message;
    pthread_mutex_unlock(&mailbox->mutex);
    free(node);
    return SUCCESS;
}

void mailbox_close(Mailbox *mailbox) {
    if (mailbox == NULL || !mailbox->initialized) return;
    if (pthread_mutex_lock(&mailbox->mutex) != 0) return;
    bool was_open = !mailbox->closed;
    mailbox->closed = true;
    pthread_mutex_unlock(&mailbox->mutex);
    if (was_open) sem_post(&mailbox->available);
}

void mailbox_destroy(Mailbox *mailbox) {
    if (mailbox == NULL || !mailbox->initialized) return;
    MailboxNode *node = mailbox->head;
    while (node != NULL) {
        MailboxNode *next = node->next;
        free(node);
        node = next;
    }
    sem_destroy(&mailbox->available);
    pthread_mutex_destroy(&mailbox->mutex);
    *mailbox = (Mailbox){0};
}
