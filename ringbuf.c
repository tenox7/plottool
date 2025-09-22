#include "ringbuf.h"
#include <stdlib.h>
#include <string.h>

ringbuf_t *ringbuf_create(uint32_t size) {
    if (size == 0) return NULL;
    
    ringbuf_t *ringbuf = malloc(sizeof(ringbuf_t));
    if (!ringbuf) return NULL;
    
    ringbuf->data = malloc(sizeof(double) * size);
    if (!ringbuf->data) {
        free(ringbuf);
        return NULL;
    }
    
    ringbuf->size = size;
    ringbuf->head = 0;
    ringbuf->tail = 0;
    ringbuf->count = 0;
    
    ringbuf->mutex = mutex_create();
    if (!ringbuf->mutex) {
        free(ringbuf->data);
        free(ringbuf);
        return NULL;
    }
    
    memset(ringbuf->data, 0, sizeof(double) * size);
    
    return ringbuf;
}

void ringbuf_destroy(ringbuf_t *ringbuf) {
    if (!ringbuf) return;

    mutex_destroy(ringbuf->mutex);
    free(ringbuf->data);
    free(ringbuf);
}

bool ringbuf_resize(ringbuf_t *ringbuf, uint32_t new_size) {
    if (!ringbuf || new_size == 0) return false;

    mutex_lock(ringbuf->mutex);

    if (new_size == ringbuf->size) {
        mutex_unlock(ringbuf->mutex);
        return true;
    }

    double *new_data = malloc(sizeof(double) * new_size);
    if (!new_data) {
        mutex_unlock(ringbuf->mutex);
        return false;
    }

    uint32_t copy_count = (ringbuf->count < new_size) ? ringbuf->count : new_size;

    if (copy_count > 0) {
        for (uint32_t i = 0; i < copy_count; i++) {
            uint32_t src_index;
            if (new_size < ringbuf->count) {
                src_index = (ringbuf->head - copy_count + i + ringbuf->size) % ringbuf->size;
            } else {
                src_index = (ringbuf->tail + i) % ringbuf->size;
            }
            new_data[i] = ringbuf->data[src_index];
        }
    }

    free(ringbuf->data);
    ringbuf->data = new_data;
    ringbuf->size = new_size;
    ringbuf->head = copy_count % new_size;
    ringbuf->tail = 0;
    ringbuf->count = copy_count;

    memset(&ringbuf->data[copy_count], 0, sizeof(double) * (new_size - copy_count));

    mutex_unlock(ringbuf->mutex);
    return true;
}

bool ringbuf_push(ringbuf_t *ringbuf, double value) {
    if (!ringbuf) return false;
    
    mutex_lock(ringbuf->mutex);
    
    ringbuf->data[ringbuf->head] = value;
    ringbuf->head = (ringbuf->head + 1) % ringbuf->size;
    
    if (ringbuf->count < ringbuf->size) {
        ringbuf->count++;
    } else {
        ringbuf->tail = (ringbuf->tail + 1) % ringbuf->size;
    }
    
    mutex_unlock(ringbuf->mutex);
    return true;
}

bool ringbuf_pop(ringbuf_t *ringbuf, double *value) {
    if (!ringbuf || !value) return false;
    
    mutex_lock(ringbuf->mutex);
    
    if (ringbuf->count == 0) {
        mutex_unlock(ringbuf->mutex);
        return false;
    }
    
    *value = ringbuf->data[ringbuf->tail];
    ringbuf->tail = (ringbuf->tail + 1) % ringbuf->size;
    ringbuf->count--;
    
    mutex_unlock(ringbuf->mutex);
    return true;
}

uint32_t ringbuf_count(ringbuf_t *ringbuf) {
    if (!ringbuf) return 0;
    
    mutex_lock(ringbuf->mutex);
    uint32_t count = ringbuf->count;
    mutex_unlock(ringbuf->mutex);
    
    return count;
}

bool ringbuf_is_full(ringbuf_t *ringbuf) {
    if (!ringbuf) return false;
    
    mutex_lock(ringbuf->mutex);
    bool full = (ringbuf->count == ringbuf->size);
    mutex_unlock(ringbuf->mutex);
    
    return full;
}

bool ringbuf_is_empty(ringbuf_t *ringbuf) {
    if (!ringbuf) return true;
    
    mutex_lock(ringbuf->mutex);
    bool empty = (ringbuf->count == 0);
    mutex_unlock(ringbuf->mutex);
    
    return empty;
}