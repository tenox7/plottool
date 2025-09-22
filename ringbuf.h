#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include <stdbool.h>
#include "platform.h"

typedef struct {
    double *data;
    uint32_t size;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    mutex_t *mutex;
} ringbuf_t;

ringbuf_t *ringbuf_create(uint32_t size);
void ringbuf_destroy(ringbuf_t *ringbuf);
bool ringbuf_resize(ringbuf_t *ringbuf, uint32_t new_size);
bool ringbuf_push(ringbuf_t *ringbuf, double value);
bool ringbuf_pop(ringbuf_t *ringbuf, double *value);
uint32_t ringbuf_count(ringbuf_t *ringbuf);
bool ringbuf_is_full(ringbuf_t *ringbuf);
bool ringbuf_is_empty(ringbuf_t *ringbuf);

#endif