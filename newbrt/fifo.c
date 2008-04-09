#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "toku_assert.h"
#include "memory.h"
#include "fifo.h"
#include "ybt.h"

static void fifo_init(struct fifo *fifo) {
    fifo->head = fifo->tail = 0;
    fifo->n = 0;
}

static int fifo_entry_size(struct fifo_entry *entry) {
    return sizeof (struct fifo_entry) + entry->keylen + entry->vallen;
}

static struct fifo_entry *fifo_peek(struct fifo *fifo) {
    return fifo->head;
}

static void fifo_enq(struct fifo *fifo, struct fifo_entry *entry) {
    entry->next = 0;
    if (fifo->head == 0)
        fifo->head = entry;
    else
        fifo->tail->next = entry;
    fifo->tail = entry;
    fifo->n += 1;
}

static struct fifo_entry *fifo_deq(struct fifo *fifo) {
    struct fifo_entry *entry = fifo->head;
    if (entry) {
        fifo->head = entry->next;
        if (fifo->head == 0)
            fifo->tail = 0;
        fifo->n -= 1;
        assert(fifo->n >= 0);
    }
    return entry;
}

static void fifo_destroy(struct fifo *fifo) {
    struct fifo_entry *entry;
    while ((entry = fifo_deq(fifo)) != 0)
        toku_free_n(entry, fifo_entry_size(entry));
}

int toku_fifo_create(FIFO *ptr) {
    struct fifo *fifo = toku_malloc(sizeof (struct fifo));
    if (fifo == 0) return ENOMEM;
    fifo_init(fifo);
    *ptr = fifo;
    return 0;
}

void toku_fifo_free(FIFO *ptr) {
    struct fifo *fifo = *ptr; *ptr = 0;
    fifo_destroy(fifo);
    toku_free_n(fifo, sizeof *fifo);
}

int toku_fifo_n_entries(FIFO fifo) {
    return fifo->n;
}

int toku_fifo_enq(FIFO fifo, const void *key, unsigned int keylen, const void *data, unsigned int datalen, int type, TXNID xid) {
    struct fifo_entry *entry = toku_malloc(sizeof (struct fifo_entry) + keylen + datalen);
    if (entry == 0) return ENOMEM;
    entry->type = type;
    entry->xid  = xid;
    entry->keylen = keylen;
    memcpy(entry->key, key, keylen);
    entry->vallen = datalen;
    memcpy(entry->key + keylen, data, datalen);
    fifo_enq(fifo, entry);
    return 0;
}

int toku_fifo_enq_cmdstruct (FIFO fifo, const BRT_CMD cmd) {
    return toku_fifo_enq(fifo, cmd->u.id.key->data, cmd->u.id.key->size, cmd->u.id.val->data, cmd->u.id.val->size, cmd->type, cmd->xid);
}

/* peek at the head (the oldest entry) of the fifo */
int toku_fifo_peek(FIFO fifo, bytevec *key, unsigned int *keylen, bytevec *data, unsigned int *datalen, u_int32_t *type, TXNID *xid) {
    struct fifo_entry *entry = fifo_peek(fifo);
    if (entry == 0) return -1;
    *key = entry->key;
    *keylen = entry->keylen;
    *data = entry->key + entry->keylen;
    *datalen = entry->vallen;
    *type = entry->type;
    *xid  = entry->xid;
    return 0;
}

// fill in the BRT_CMD, using the two DBTs for the DBT part.
int toku_fifo_peek_cmdstruct (FIFO fifo, BRT_CMD cmd, DBT*key, DBT*data) {
    u_int32_t type;
    bytevec keyb,datab;
    unsigned int keylen,datalen;
    int r = toku_fifo_peek(fifo, &keyb, &keylen, &datab, &datalen, &type, &cmd->xid);
    if (r!=0) return r;
    cmd->type=type;
    toku_fill_dbt(key, keyb, keylen);
    toku_fill_dbt(data, datab, datalen);
    cmd->u.id.key=key;
    cmd->u.id.val=data;
    return 0;
}


int toku_fifo_deq(FIFO fifo) {
    struct fifo_entry *entry = fifo_deq(fifo);
    if (entry == 0) return -1; // if entry is 0 then it was an empty fifo
    toku_free_n(entry, fifo_entry_size(entry));
    return 0;
}

// fill in the BRT_CMD, using the two DBTs for the DBT part.
//int toku_fifo_peek_deq_cmdstruct (FIFO fifo, BRT_CMD cmd, DBT*key, DBT*data) {
//    int r = toku_fifo_peek_cmdstruct(fifo, cmd, key, data);
//    if (r!=0) return r;
//    return toku_fifo_deq(fifo);
//}


//int toku_fifo_peek_deq (FIFO fifo, bytevec *key, ITEMLEN *keylen, bytevec *data, ITEMLEN *datalen, u_int32_t *type, TXNID *xid) {
//    int r= toku_fifo_peek(fifo, key, keylen, data, datalen, type, xid);
//    if (r==0) return toku_fifo_deq(fifo);
//    else return r;
//}


void toku_fifo_iterate (FIFO fifo, void(*f)(bytevec key,ITEMLEN keylen,bytevec data,ITEMLEN datalen,int type, TXNID xid, void*), void *arg) {
    struct fifo_entry *entry;
    for (entry = fifo_peek(fifo); entry; entry = entry->next)
        f(entry->key, entry->keylen, entry->key + entry->keylen, entry->vallen, entry->type, entry->xid, arg);
}


