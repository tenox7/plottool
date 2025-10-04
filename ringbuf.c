#include "compat.h"
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
    atomic_store(&ringbuf->head, 0);
    atomic_store(&ringbuf->tail, 0);
    atomic_store(&ringbuf->count, 0);

    ringbuf->write_mutex = mutex_create();
    if (!ringbuf->write_mutex) {
        free(ringbuf->data);
        free(ringbuf);
        return NULL;
    }

    ringbuf->resize_mutex = mutex_create();
    if (!ringbuf->resize_mutex) {
        mutex_destroy(ringbuf->write_mutex);
        free(ringbuf->data);
        free(ringbuf);
        return NULL;
    }
    
    memset(ringbuf->data, 0, sizeof(double) * size);
    
    return ringbuf;
}

void ringbuf_destroy(ringbuf_t *ringbuf) {
    if (!ringbuf) return;

    mutex_destroy(ringbuf->write_mutex);
    mutex_destroy(ringbuf->resize_mutex);
    free(ringbuf->data);
    free(ringbuf);
}

int ringbuf_resize(ringbuf_t *ringbuf, uint32_t new_size) {
    if (!ringbuf || new_size == 0) return 0;

    mutex_lock(ringbuf->resize_mutex);
    mutex_lock(ringbuf->write_mutex);

    if (new_size == ringbuf->size) {
        mutex_unlock(ringbuf->write_mutex);
        mutex_unlock(ringbuf->resize_mutex);
        return 1;
    }

    double *new_data = malloc(sizeof(double) * new_size);
    if (!new_data) {
        mutex_unlock(ringbuf->write_mutex);
        mutex_unlock(ringbuf->resize_mutex);
        return 0;
    }

    uint32_t current_count = atomic_load(&ringbuf->count);
    uint32_t current_head = atomic_load(&ringbuf->head);
    uint32_t current_tail = atomic_load(&ringbuf->tail);
    uint32_t copy_count = (current_count < new_size) ? current_count : new_size;

    if (copy_count > 0) {
        uint32_t i;
        for (i = 0; i < copy_count; i++) {
            uint32_t src_index;
            if (new_size < current_count) {
                src_index = (current_head - copy_count + i + ringbuf->size) % ringbuf->size;
            } else {
                src_index = (current_tail + i) % ringbuf->size;
            }
            new_data[i] = ringbuf->data[src_index];
        }
    }

    free(ringbuf->data);
    ringbuf->data = new_data;
    ringbuf->size = new_size;
    atomic_store(&ringbuf->head, copy_count % new_size);
    atomic_store(&ringbuf->tail, 0);
    atomic_store(&ringbuf->count, copy_count);

    memset(&ringbuf->data[copy_count], 0, sizeof(double) * (new_size - copy_count));

    mutex_unlock(ringbuf->write_mutex);
    mutex_unlock(ringbuf->resize_mutex);
    return 1;
}

int ringbuf_push(ringbuf_t *ringbuf, double value) {
    if (!ringbuf) return 0;

    mutex_lock(ringbuf->write_mutex);

    uint32_t current_head = atomic_load(&ringbuf->head);
    uint32_t current_count = atomic_load(&ringbuf->count);

    ringbuf->data[current_head] = value;
    uint32_t new_head = (current_head + 1) % ringbuf->size;
    atomic_store(&ringbuf->head, new_head);

    if (current_count < ringbuf->size) {
        atomic_store(&ringbuf->count, current_count + 1);
    } else {
        uint32_t current_tail = atomic_load(&ringbuf->tail);
        atomic_store(&ringbuf->tail, (current_tail + 1) % ringbuf->size);
    }

    mutex_unlock(ringbuf->write_mutex);
    return 1;
}

int ringbuf_pop(ringbuf_t *ringbuf, double *value) {
    if (!ringbuf || !value) return 0;

    mutex_lock(ringbuf->write_mutex);

    uint32_t current_count = atomic_load(&ringbuf->count);
    if (current_count == 0) {
        mutex_unlock(ringbuf->write_mutex);
        return 0;
    }

    uint32_t current_tail = atomic_load(&ringbuf->tail);
    *value = ringbuf->data[current_tail];
    atomic_store(&ringbuf->tail, (current_tail + 1) % ringbuf->size);
    atomic_store(&ringbuf->count, current_count - 1);

    mutex_unlock(ringbuf->write_mutex);
    return 1;
}

uint32_t ringbuf_count(ringbuf_t *ringbuf) {
    if (!ringbuf) return 0;

    return atomic_load(&ringbuf->count);
}

int ringbuf_is_full(ringbuf_t *ringbuf) {
    if (!ringbuf) return 0;

    return (atomic_load(&ringbuf->count) == ringbuf->size);
}

int ringbuf_is_empty(ringbuf_t *ringbuf) {
    if (!ringbuf) return 1;

    return (atomic_load(&ringbuf->count) == 0);
}

int ringbuf_read_snapshot(ringbuf_t *ringbuf, double *buffer, uint32_t buffer_size, uint32_t *count_out, uint32_t *head_out, uint32_t *tail_out) {
    if (!ringbuf || !buffer || !count_out || !head_out || !tail_out) return 0;

    uint32_t count, head, tail;
    uint32_t attempts = 0;
    const uint32_t max_attempts = 10;

    do {
        count = atomic_load(&ringbuf->count);
        head = atomic_load(&ringbuf->head);
        tail = atomic_load(&ringbuf->tail);

        if (count == 0) {
            *count_out = 0;
            return 1;
        }

        uint32_t copy_count = (count < buffer_size) ? count : buffer_size;
        uint32_t i;

        for (i = 0; i < copy_count; i++) {
            uint32_t idx = (tail + i) % ringbuf->size;
            buffer[i] = ringbuf->data[idx];
        }

        uint32_t verify_count = atomic_load(&ringbuf->count);
        uint32_t verify_head = atomic_load(&ringbuf->head);
        uint32_t verify_tail = atomic_load(&ringbuf->tail);

        if (count == verify_count && head == verify_head && tail == verify_tail) {
            *count_out = copy_count;
            *head_out = head;
            *tail_out = tail;
            return 1;
        }

        attempts++;
    } while (attempts < max_attempts);

    return 0;
}
