#include "threading.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void data_source_thread(void *arg) {
    data_source_t *source = (data_source_t*)arg;
    if (!source) return;

    printf("Starting data source thread: %s:%s\n", source->type, source->target);

    if (!source->datasource) {
        fprintf(stderr, "No datasource available for %s:%s\n", source->type, source->target);
        while (source->running) {
            ringbuf_push(source->data_buffer, -1.0);
            platform_sleep(source->refresh_interval_ms);
        }
        return;
    }

    uint32_t sample_count = 0;
    while (source->running) {
        if (!source->running) break;

        if (source->is_dual && source->datasource->handler->collect_dual) {
            // Dual collection for SNMP-like sources
            double in_value = 0.0, out_value = 0.0;
            bool success = source->datasource->handler->collect_dual(source->datasource->context, &in_value, &out_value);

            if (success) {
                ringbuf_push(source->data_buffer, in_value);          // IN data
                ringbuf_push(source->data_buffer_secondary, out_value); // OUT data
            } else {
                ringbuf_push(source->data_buffer, -1.0);
                ringbuf_push(source->data_buffer_secondary, -1.0);
            }

            sample_count++;
            if (sample_count % 10 == 0) {
                printf("Data source %s:%s collected %u dual samples, latest IN: %.2f, OUT: %.2f\n",
                       source->type, source->target, sample_count, in_value, out_value);
            }
        } else {
            // Single collection for regular sources
            double value = 0.0;
            bool success = datasource_collect(source->datasource, &value);

            if (success) {
                ringbuf_push(source->data_buffer, value);
            } else {
                ringbuf_push(source->data_buffer, -1.0);
            }

            sample_count++;
            if (sample_count % 10 == 0) {
                printf("Data source %s:%s collected %u samples, latest value: %.2f\n",
                       source->type, source->target, sample_count, value);
            }
        }

        platform_sleep(source->refresh_interval_ms);
    }

    printf("Data source thread %s:%s ended after %u samples\n",
           source->type, source->target, sample_count);
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
    
    for (uint32_t i = 0; i < collector->source_count; i++) {
        data_source_t *source = &collector->sources[i];
        source->type = malloc(strlen(config->plots[i].type) + 1);
        source->target = malloc(strlen(config->plots[i].target) + 1);
        
        if (!source->type || !source->target) {
            for (uint32_t j = 0; j < i; j++) {
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
        source->running = false;

        // Check if this is a dual-line data source
        source->is_dual = (source->datasource && source->datasource->handler->is_dual);
        if (source->is_dual) {
            source->data_buffer_secondary = ringbuf_create(config->default_width - 2);
        } else {
            source->data_buffer_secondary = NULL;
        }

        // Use plot-specific refresh interval if set, otherwise use global default
        source->refresh_interval_ms = (config->plots[i].refresh_interval_ms > 0) ?
                                     config->plots[i].refresh_interval_ms :
                                     config->refresh_interval_ms;
        
        if (!source->data_buffer) {
            for (uint32_t j = 0; j < i; j++) {
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
    if (!collector) return;
    
    for (uint32_t i = 0; i < collector->source_count; i++) {
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

bool data_collector_start(data_collector_t *collector) {
    if (!collector) return false;
    
    for (uint32_t i = 0; i < collector->source_count; i++) {
        data_source_t *source = &collector->sources[i];
        source->running = true;
        source->thread = plot_thread_create(data_source_thread, source);
        
        if (!source->thread) {
            source->running = false;
            for (uint32_t j = 0; j < i; j++) {
                collector->sources[j].running = false;
                plot_thread_join(collector->sources[j].thread);
                plot_thread_destroy(collector->sources[j].thread);
            }
            return false;
        }
    }
    
    return true;
}

void data_collector_stop(data_collector_t *collector) {
    if (!collector) return;
    
    for (uint32_t i = 0; i < collector->source_count; i++) {
        collector->sources[i].running = false;
        plot_thread_join(collector->sources[i].thread);
        plot_thread_destroy(collector->sources[i].thread);
    }
}