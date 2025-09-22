#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "graphics.h"

typedef struct {
    char *name;
    char *type;
    char *target;
    color_t line_color;
    color_t line_color_secondary; // For dual-line plots (OUT color)
    color_t background_color;
    int32_t height;
    int32_t refresh_interval_ms;
} plot_config_t;

typedef struct {
    color_t background_color;
    color_t text_color;
    color_t border_color;
    color_t line_color;
    color_t line_color_secondary;
    int32_t default_height;
    int32_t default_width;
    int32_t refresh_interval_ms;
    int32_t window_margin;
    int32_t max_fps;
    bool fullscreen;

    plot_config_t *plots;
    uint32_t plot_count;
} config_t;

config_t *config_load(const char *filename);
void config_destroy(config_t *config);

#endif