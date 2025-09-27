#ifndef THREADING_H
#define THREADING_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "platform.h"
#include "ringbuf.h"
#include "config.h"
#include "datasource.h"

typedef struct {
    char *type;
    char *target;
    datasource_t *datasource;
    ringbuf_t *data_buffer;
    ringbuf_t *data_buffer_secondary;
    plot_thread_t *thread;
    atomic_bool running;
    atomic_bool shutdown_requested;
    int32_t refresh_interval_ms;
    bool is_dual;
} data_source_t;

typedef struct {
    data_source_t *sources;
    uint32_t source_count;
    atomic_bool shutdown_requested;
} data_collector_t;

data_collector_t *data_collector_create(config_t *config);
void data_collector_destroy(data_collector_t *collector);
bool data_collector_start(data_collector_t *collector);
void data_collector_stop(data_collector_t *collector);
void data_collector_request_shutdown(data_collector_t *collector);

#endif