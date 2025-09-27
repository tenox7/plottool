#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "platform.h"

typedef struct {
    double *data;
    uint32_t size;
    atomic_uint_fast32_t head;
    atomic_uint_fast32_t tail;
    atomic_uint_fast32_t count;
    mutex_t *write_mutex;
    mutex_t *resize_mutex;
} ringbuf_t;

ringbuf_t *ringbuf_create(uint32_t size);
void ringbuf_destroy(ringbuf_t *ringbuf);
bool ringbuf_resize(ringbuf_t *ringbuf, uint32_t new_size);
bool ringbuf_push(ringbuf_t *ringbuf, double value);
bool ringbuf_pop(ringbuf_t *ringbuf, double *value);
uint32_t ringbuf_count(ringbuf_t *ringbuf);
bool ringbuf_is_full(ringbuf_t *ringbuf);
bool ringbuf_is_empty(ringbuf_t *ringbuf);
bool ringbuf_read_snapshot(ringbuf_t *ringbuf, double *buffer, uint32_t buffer_size, uint32_t *count_out, uint32_t *head_out, uint32_t *tail_out);

#endif