#include "compat.h"
#include "threading.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void data_source_thread(void *arg) {
    data_source_t *source = (data_source_t*)arg;
    if (!source) return;


    if (!source->datasource) {
        while (1) {
            ringbuf_push(source->data_buffer, -1.0);
            platform_sleep(source->refresh_interval_ms);
        }
        return;
    }

    uint32_t sample_count = 0;
    while (1) {

        if (source->is_dual && source->datasource->handler->collect_dual) {
            double in_value = 0.0, out_value = 0.0;
            int success = source->datasource->handler->collect_dual(source->datasource->context, &in_value, &out_value);

            if (success) {
                ringbuf_push(source->data_buffer, in_value);
                ringbuf_push(source->data_buffer_secondary, out_value);
            } else {
                ringbuf_push(source->data_buffer, -1.0);
                ringbuf_push(source->data_buffer_secondary, -1.0);
            }

            sample_count++;
            if (sample_count % 10 == 0) {
            }
        } else {
            double value = 0.0;
            int success = datasource_collect(source->datasource, &value);

            if (success) {
                ringbuf_push(source->data_buffer, value);
            } else {
                ringbuf_push(source->data_buffer, -1.0);
            }

            sample_count++;
            if (sample_count % 10 == 0) {
            }
        }

        platform_sleep(source->refresh_interval_ms);
    }

}

data_collector_t *data_collector_create(config_t *config) {
    if (!config) return NULL;
    
    data_collector_t *collector = malloc(sizeof(data_collector_t));
    if (!collector) return NULL;
    
    collector->source_count = config->plot_count;
    collector->sources = malloc(sizeof(data_source_t) * collector->source_count);
    if (!collector->sources) {
        free(collector);
        return NULL;
    }
    
    uint32_t i, j;
    for (i = 0; i < collector->source_count; i++) {
        data_source_t *source = &collector->sources[i];
        source->type = malloc(strlen(config->plots[i].type) + 1);
        source->target = malloc(strlen(config->plots[i].target) + 1);
        
        if (!source->type || !source->target) {
            for (j = 0; j < i; j++) {
                free(collector->sources[j].type);
                free(collector->sources[j].target);
            }
            free(collector->sources);
            free(collector);
            return NULL;
        }
        
        strcpy(source->type, config->plots[i].type);
        strcpy(source->target, config->plots[i].target);
        source->datasource = datasource_create(config->plots[i].type, config->plots[i].target);
        source->data_buffer = ringbuf_create(config->default_width - 2);
        source->thread = NULL;

        source->is_dual = (source->datasource && source->datasource->handler->is_dual);
        if (source->is_dual) {
            source->data_buffer_secondary = ringbuf_create(config->default_width - 2);
        } else {
            source->data_buffer_secondary = NULL;
        }

        source->refresh_interval_ms = (config->plots[i].refresh_interval_ms > 0) ?
                                     config->plots[i].refresh_interval_ms :
                                     config->refresh_interval_ms;

        if (source->datasource) {
            datasource_set_refresh_interval(source->datasource, source->refresh_interval_ms);
        }

        if (!source->data_buffer) {
            for (j = 0; j < i; j++) {
                free(collector->sources[j].type);
                free(collector->sources[j].target);
                ringbuf_destroy(collector->sources[j].data_buffer);
            }
            free(collector->sources);
            free(collector);
            return NULL;
        }
    }


    return collector;
}

void data_collector_destroy(data_collector_t *collector) {
    uint32_t i;
    if (!collector) return;
    
    for (i = 0; i < collector->source_count; i++) {
        free(collector->sources[i].type);
        free(collector->sources[i].target);
        datasource_destroy(collector->sources[i].datasource);
        ringbuf_destroy(collector->sources[i].data_buffer);
        if (collector->sources[i].data_buffer_secondary) {
            ringbuf_destroy(collector->sources[i].data_buffer_secondary);
        }
    }
    
    free(collector->sources);
    free(collector);
}

int data_collector_start(data_collector_t *collector) {
    uint32_t i;
    if (!collector) return 0;
    
    for (i = 0; i < collector->source_count; i++) {
        data_source_t *source = &collector->sources[i];
        source->thread = plot_thread_create(data_source_thread, source);

        if (!source->thread) {
            return 0;
        }
    }
    
    return 1;
}

