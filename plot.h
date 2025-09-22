#ifndef PLOT_H
#define PLOT_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "ringbuf.h"
#include "graphics.h"
#include "threading.h"

typedef struct {
    plot_config_t *config;
    ringbuf_t *data_buffer;
    ringbuf_t *data_buffer_secondary; // For dual-line plots (OUT data)
    double min_value;
    double max_value;
    double avg_value;
    double last_value;
    double min_value_secondary;
    double max_value_secondary;
    double avg_value_secondary;
    double last_value_secondary;
    bool active;
    bool is_dual; // True for dual-line plots like SNMP
} plot_t;

typedef struct {
    config_t *config;
    plot_t *plots;
    uint32_t plot_count;
    window_t *window;
    renderer_t *renderer;
    font_t *font;
    bool fullscreen;
    int32_t last_plot_width;
} plot_system_t;

plot_system_t *plot_system_create(config_t *config);
void plot_system_destroy(plot_system_t *system);
bool plot_system_update(plot_system_t *system);
void plot_system_connect_data_buffers(plot_system_t *system, data_collector_t *collector);

void plot_draw(plot_t *plot, renderer_t *renderer, font_t *font,
               int32_t x, int32_t y, int32_t width, int32_t height, config_t *global_config);

#endif