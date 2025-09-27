#define _GNU_SOURCE
#include "plot.h"
#include "threading.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

static void calculate_stats(plot_t *plot) {
    if (!plot || !plot->data_buffer) return;

    double temp_buffer[2048];
    double temp_buffer_secondary[2048];
    uint32_t data_count, head_pos, tail_pos;
    uint32_t data_count_secondary = 0, head_pos_secondary = 0, tail_pos_secondary = 0;

    /* Get atomic snapshots of buffer data */
    if (!ringbuf_read_snapshot(plot->data_buffer, temp_buffer, 2048, &data_count, &head_pos, &tail_pos)) {
        return;
    }

    if (plot->is_dual && plot->data_buffer_secondary) {
        if (!ringbuf_read_snapshot(plot->data_buffer_secondary, temp_buffer_secondary, 2048,
                                  &data_count_secondary, &head_pos_secondary, &tail_pos_secondary)) {
            return;
        }
    }

    /* Check if stats are cached and data hasn't changed */
    uint32_t current_count = data_count;
    uint32_t current_head = head_pos;
    uint32_t current_count_secondary = data_count_secondary;
    uint32_t current_head_secondary = head_pos_secondary;

    if (!plot->stats_dirty &&
        current_count == plot->cached_data_count &&
        current_head == plot->cached_head_position &&
        current_count_secondary == plot->cached_data_count_secondary &&
        current_head_secondary == plot->cached_head_position_secondary) {
        return;
    }

    /* Update cache tracking */
    plot->cached_data_count = current_count;
    plot->cached_head_position = current_head;
    plot->cached_data_count_secondary = current_count_secondary;
    plot->cached_head_position_secondary = current_head_secondary;
    plot->stats_dirty = false;

    if (plot->is_dual && plot->data_buffer_secondary) {
        // Dual-line combined statistics
        double sum = 0.0;
        uint32_t count = 0;
        plot->min_value = 10000.0;
        plot->max_value = 0.0;
        plot->last_value = 0.0;

        // Process both buffers together for combined statistics
        uint32_t data_count = plot->data_buffer->count;
        for (uint32_t i = 0; i < data_count; i++) {
            uint32_t idx = (plot->data_buffer->tail + i) % plot->data_buffer->size;
            double primary_value = plot->data_buffer->data[idx];
            double secondary_value = plot->data_buffer_secondary->data[idx];

            if (primary_value > 0) {
                if (primary_value < plot->min_value) plot->min_value = primary_value;
                if (primary_value > plot->max_value) plot->max_value = primary_value;
                sum += primary_value;
                count++;
            }

            if (secondary_value > 0) {
                if (secondary_value < plot->min_value) plot->min_value = secondary_value;
                if (secondary_value > plot->max_value) plot->max_value = secondary_value;
                sum += secondary_value;
                count++;
            }
        }

        // Calculate combined LAST value as sum of the two most recent values
        if (data_count > 0) {
            uint32_t last_idx = (plot->data_buffer->head - 1 + plot->data_buffer->size) % plot->data_buffer->size;
            double last_primary = plot->data_buffer->data[last_idx];
            double last_secondary = plot->data_buffer_secondary->data[last_idx];

            if (last_primary > 0 && last_secondary > 0) {
                plot->last_value = last_primary + last_secondary; // SUM instead of average
            } else if (last_primary > 0) {
                plot->last_value = last_primary;
            } else if (last_secondary > 0) {
                plot->last_value = last_secondary;
            } else {
                plot->last_value = 0.0;
            }
        }

        plot->avg_value = count > 0 ? sum / count : 0.0;
    } else {
        // Single-line statistics (original behavior)
        double sum = 0.0;
        uint32_t count = 0;
        plot->min_value = 10000.0;
        plot->max_value = 0.0;
        plot->last_value = 0.0;

        for (uint32_t i = 0; i < plot->data_buffer->count; i++) {
            uint32_t idx = (plot->data_buffer->tail + i) % plot->data_buffer->size;
            double value = plot->data_buffer->data[idx];

            if (value > 0) {
                if (value < plot->min_value) plot->min_value = value;
                if (value > plot->max_value) plot->max_value = value;
                sum += value;
                count++;
            }
        }

        // Get the last (most recent) value
        if (plot->data_buffer->count > 0) {
            uint32_t last_idx = (plot->data_buffer->head - 1 + plot->data_buffer->size) % plot->data_buffer->size;
            plot->last_value = plot->data_buffer->data[last_idx];
        }

        plot->avg_value = count > 0 ? sum / count : 0.0;
    }
}


// Dynamic unit formatting for throughput values like ttg.c
static void format_throughput_unit(double bytes_per_sec, char* unit_str, size_t unit_size, double* scaled_value) {
    if (bytes_per_sec >= 1073741824.0) { // >= 1GB/s
        *scaled_value = bytes_per_sec / 1073741824.0;
        snprintf(unit_str, unit_size, "GB/s");
    } else if (bytes_per_sec >= 1048576.0) { // >= 1MB/s
        *scaled_value = bytes_per_sec / 1048576.0;
        snprintf(unit_str, unit_size, "MB/s");
    } else if (bytes_per_sec >= 1024.0) { // >= 1KB/s
        *scaled_value = bytes_per_sec / 1024.0;
        snprintf(unit_str, unit_size, "KB/s");
    } else {
        *scaled_value = bytes_per_sec;
        snprintf(unit_str, unit_size, "B/s");
    }
}

static const char* get_plot_unit(const char* type) {
    extern datasource_handler_t ping_handler;
    extern datasource_handler_t cpu_handler;
    extern datasource_handler_t memory_handler;
    extern datasource_handler_t sine_handler;
    extern datasource_handler_t snmp_handler;

    datasource_handler_t *handlers[] = {&ping_handler, &cpu_handler, &memory_handler, &sine_handler, &snmp_handler, NULL};

    for (int i = 0; handlers[i]; i++) {
        if (strcmp(handlers[i]->name, type) == 0) {
            return handlers[i]->unit;
        }
    }
    return "";
}

static double get_plot_max_scale(const char* type) {
    if (strcmp(type, "cpu") == 0 || strcmp(type, "memory") == 0) {
        return 100.0;
    }
    return 0.0;
}

void plot_draw(plot_t *plot, renderer_t *renderer, font_t *font,
               int32_t x, int32_t y, int32_t width, int32_t height, config_t *global_config) {
    if (!plot || !renderer || !font) return;
    
    calculate_stats(plot);
    
    color_t border_color = (color_t){255, 255, 255, 255};
    renderer_set_color(renderer, border_color);
    
    // Title area (20px height) - just use the auto-generated name
    char title[256];
    snprintf(title, sizeof(title), "%s", plot->config->name);
    font_draw_text(renderer, font, border_color, x, y + 5, title);
    
    // Plot box area (height - 20px for title - 20px for stats)
    int32_t plot_y = y + 20;
    int32_t plot_height = height - 40;
    rect_t border_rect = {x, plot_y, width, plot_height};
    renderer_draw_rect(renderer, border_rect);
    
    if (!plot->data_buffer || ringbuf_count(plot->data_buffer) == 0) {
        char stats_text[128];
        snprintf(stats_text, sizeof(stats_text), "No data");
        font_draw_text(renderer, font, border_color, x, y + height - 15, stats_text);
        return;
    }

    double fixed_max_scale = get_plot_max_scale(plot->config->type);
    double max_val;
    if (fixed_max_scale > 0.0) {
        max_val = fixed_max_scale;
    } else {
        max_val = plot->max_value > 0 ? plot->max_value : 1.0;
    }

    // Display vertical scale in top right corner
    char scale_text[64];
    if (strcmp(plot->config->type, "snmp") == 0 || strcmp(plot->config->type, "if_thr") == 0) {
        // Use dynamic formatting for throughput data
        char scale_unit[16];
        double scaled_value;
        format_throughput_unit(max_val, scale_unit, sizeof(scale_unit), &scaled_value);
        snprintf(scale_text, sizeof(scale_text), "%.1f%s", scaled_value, scale_unit);
    } else {
        // Use static formatting for other data sources
        const char* unit = get_plot_unit(plot->config->type);
        if (strlen(unit) > 0) {
            snprintf(scale_text, sizeof(scale_text), "%.1f%s", max_val, unit);
        } else {
            snprintf(scale_text, sizeof(scale_text), "%.1f", max_val);
        }
    }

    int32_t scale_text_width, scale_text_height;
    font_get_text_size(font, scale_text, &scale_text_width, &scale_text_height);
    int32_t scale_x = x + width - scale_text_width;
    font_draw_text(renderer, font, border_color, scale_x, y + 5, scale_text);

    double temp_buffer[2048];
    double temp_buffer_secondary[2048];
    uint32_t data_count, head_pos, tail_pos;
    uint32_t data_count_secondary, head_pos_secondary, tail_pos_secondary;

    if (!ringbuf_read_snapshot(plot->data_buffer, temp_buffer, 2048, &data_count, &head_pos, &tail_pos)) {
        return;
    }

    if (plot->is_dual && plot->data_buffer_secondary) {
        if (!ringbuf_read_snapshot(plot->data_buffer_secondary, temp_buffer_secondary, 2048,
                                  &data_count_secondary, &head_pos_secondary, &tail_pos_secondary)) {
            return;
        }
        // Dual-line rendering for SNMP: IN (filled green) and OUT (blue line)
        int32_t prev_out_x = -1, prev_out_y = -1;

        for (uint32_t i = 0; i < data_count; i++) {
            double in_value = temp_buffer[i];
            double out_value = temp_buffer_secondary[i];

            // Map oldest (i=0) to leftmost, newest (i=count-1) to rightmost
            int32_t plot_x = x + width - 2 - (data_count - 1 - i);
            int32_t plot_bottom = plot_y + plot_height - 2;

            if (in_value < 0 || out_value < 0) {
                // Error/failure - red line full height
                renderer_set_color(renderer, (color_t){255, 0, 0, 255});
                renderer_draw_line(renderer, plot_x, plot_y + 2, plot_x, plot_bottom);
                prev_out_x = prev_out_y = -1; // Reset line continuity on error
            } else {
                // First draw IN as filled line (green)
                int32_t in_bar_height = (int32_t)((in_value / max_val) * (plot_height - 4));
                if (in_bar_height < 1) in_bar_height = 1;

                renderer_set_color(renderer, plot->config->line_color); // Green for IN
                renderer_draw_line(renderer, plot_x, plot_bottom - in_bar_height, plot_x, plot_bottom);

                // Then draw OUT as continuous line (blue)
                int32_t out_bar_height = (int32_t)((out_value / max_val) * (plot_height - 4));
                int32_t out_y = plot_bottom - out_bar_height;

                renderer_set_color(renderer, plot->config->line_color_secondary); // Blue for OUT

                if (prev_out_x >= 0 && prev_out_y >= 0) {
                    // Draw line from previous point to current point
                    renderer_draw_line(renderer, prev_out_x, prev_out_y, plot_x, out_y);
                } else {
                    // First point - just draw a single pixel
                    renderer_draw_line(renderer, plot_x, out_y, plot_x, out_y);
                }

                prev_out_x = plot_x;
                prev_out_y = out_y;
            }
        }
    } else {
        // Single-line rendering for normal plots
        for (uint32_t i = 0; i < data_count; i++) {
            double value = temp_buffer[i];

            // Map oldest (i=0) to leftmost, newest (i=count-1) to rightmost
            int32_t plot_x = x + width - 2 - (data_count - 1 - i);
            int32_t plot_bottom = plot_y + plot_height - 2;

            if (value < 0) {
                // Error/failure - red line full height
                renderer_set_color(renderer, (color_t){255, 0, 0, 255});
                renderer_draw_line(renderer, plot_x, plot_y + 2, plot_x, plot_bottom);
            } else {
                // Normal data - use plot's configured line color
                int32_t bar_height = (int32_t)((value / max_val) * (plot_height - 4));
                if (bar_height < 1) bar_height = 1;

                renderer_set_color(renderer, plot->config->line_color);
                renderer_draw_line(renderer, plot_x, plot_bottom - bar_height, plot_x, plot_bottom);
            }
        }
    }
    
    // Universal stats formatting - handle per-value units or single unit
    char stats_text[128];
    char min_unit[16] = "", max_unit[16] = "", avg_unit[16] = "", last_unit[16] = "";
    double min_display = plot->min_value, max_display = plot->max_value;
    double avg_display = plot->avg_value, last_display = plot->last_value;

    if (strcmp(plot->config->type, "snmp") == 0 || strcmp(plot->config->type, "if_thr") == 0) {
        // SNMP and if_thr use dynamic per-value unit formatting
        format_throughput_unit(plot->min_value, min_unit, sizeof(min_unit), &min_display);
        format_throughput_unit(plot->max_value, max_unit, sizeof(max_unit), &max_display);
        format_throughput_unit(plot->avg_value, avg_unit, sizeof(avg_unit), &avg_display);
        format_throughput_unit(plot->last_value, last_unit, sizeof(last_unit), &last_display);
    } else {
        // Other types use single unit (if any)
        const char* unit = get_plot_unit(plot->config->type);
        if (strlen(unit) > 0) {
            strcpy(last_unit, unit); // Only append unit to last value for consistency with old format
        }
    }

    snprintf(stats_text, sizeof(stats_text), "min=%.1f%s max=%.1f%s avg=%.1f%s last=%.1f%s",
             min_display, min_unit, max_display, max_unit, avg_display, avg_unit, last_display, last_unit);

    int32_t text_width, text_height;
    font_get_text_size(font, stats_text, &text_width, &text_height);
    int32_t text_x = x + width - text_width;
    font_draw_text(renderer, font, border_color, text_x, y + height - 15, stats_text);

    uint32_t buffer_size = plot->data_buffer->size;
    int32_t refresh_interval = (plot->config->refresh_interval_ms > 0) ?
                              plot->config->refresh_interval_ms :
                              global_config->refresh_interval_ms;
    uint32_t total_time_ms = buffer_size * refresh_interval;

    char time_span_text[64];
    if (total_time_ms < 60000) {
        snprintf(time_span_text, sizeof(time_span_text), "%us", total_time_ms / 1000);
    } else if (total_time_ms < 86400000) {
        uint32_t minutes = (total_time_ms + 59999) / 60000;
        if (minutes < 60) {
            snprintf(time_span_text, sizeof(time_span_text), "%um", minutes);
        } else {
            uint32_t hours = (minutes + 59) / 60;
            snprintf(time_span_text, sizeof(time_span_text), "%uh", hours);
        }
    } else {
        uint32_t days = (total_time_ms + 86399999) / 86400000;
        snprintf(time_span_text, sizeof(time_span_text), "%ud", days);
    }

    font_draw_text(renderer, font, border_color, x, y + height - 15, time_span_text);
}

plot_system_t *plot_system_create(config_t *config) {
    if (!config) return NULL;
    
    plot_system_t *system = malloc(sizeof(plot_system_t));
    if (!system) return NULL;
    
    system->config = config;
    system->plot_count = config->plot_count;
    system->plots = malloc(sizeof(plot_t) * system->plot_count);
    if (!system->plots) {
        free(system);
        return NULL;
    }
    
    int32_t plot_spacing = 10;
    int32_t window_height = config->plot_count * (config->default_height + plot_spacing) + config->window_margin * 2;

    char hostname[256];
    char window_title[300];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        /* Extract short hostname (remove domain part) */
        char *dot = strchr(hostname, '.');
        if (dot) *dot = '\0';
        snprintf(window_title, sizeof(window_title), "PlotTool : %s", hostname);
    } else {
        strcpy(window_title, "PlotTool");
    }

    system->window = window_create(window_title,
                                  config->default_width,
                                  window_height);
    if (!system->window) {
        free(system->plots);
        free(system);
        return NULL;
    }

    // Set fullscreen mode if configured
    window_set_fullscreen(system->window, config->fullscreen);

    system->renderer = renderer_create(system->window);
    if (!system->renderer) {
        window_destroy(system->window);
        free(system->plots);
        free(system);
        return NULL;
    }
    
    system->font = font_create("", 11);
    if (!system->font) {
        renderer_destroy(system->renderer);
        window_destroy(system->window);
        free(system->plots);
        free(system);
        return NULL;
    }
    
    system->fullscreen = false;
    system->last_plot_width = 0;

    /* Initialize window size cache */
    system->cached_window_width = 0;
    system->cached_window_height = 0;
    system->window_size_dirty = true;

    /* Initialize rendering optimization */
    system->needs_redraw = true;



    for (uint32_t i = 0; i < system->plot_count; i++) {
        plot_t *plot = &system->plots[i];
        plot->config = &config->plots[i];
        plot->data_buffer = NULL;
        plot->min_value = 0.0;
        plot->max_value = 0.0;
        plot->avg_value = 0.0;
        plot->last_value = 0.0;
        plot->active = true;

        /* Initialize cache fields */
        plot->cached_data_count = 0;
        plot->cached_data_count_secondary = 0;
        plot->cached_head_position = 0;
        plot->cached_head_position_secondary = 0;
        plot->stats_dirty = true;
    }
    
    return system;
}

void plot_system_destroy(plot_system_t *system) {
    if (!system) return;
    
    font_destroy(system->font);
    renderer_destroy(system->renderer);
    window_destroy(system->window);
    free(system->plots);
    free(system);
}

void plot_system_connect_data_buffers(plot_system_t *system, data_collector_t *collector) {
    if (!system || !collector) return;

    for (uint32_t i = 0; i < system->plot_count && i < collector->source_count; i++) {
        system->plots[i].data_buffer = collector->sources[i].data_buffer;
        system->plots[i].data_buffer_secondary = collector->sources[i].data_buffer_secondary;
        system->plots[i].is_dual = collector->sources[i].is_dual;
    }
}

static bool plot_system_needs_redraw(plot_system_t *system) {
    if (!system) return false;

    if (window_was_resized() || system->window_size_dirty || system->needs_redraw) {
        return true;
    }

    for (uint32_t i = 0; i < system->plot_count; i++) {
        plot_t *plot = &system->plots[i];
        if (!plot->data_buffer) continue;

        if (plot->cached_data_count != plot->data_buffer->count ||
            plot->cached_head_position != plot->data_buffer->head) {
            return true;
        }

        if (plot->is_dual && plot->data_buffer_secondary &&
            (plot->cached_data_count_secondary != plot->data_buffer_secondary->count ||
             plot->cached_head_position_secondary != plot->data_buffer_secondary->head)) {
            return true;
        }
    }

    return false;
}

bool plot_system_update(plot_system_t *system) {
    if (!system) return false;

    if (!graphics_wait_events()) {
        return false;
    }

    graphics_event_t event;
    while (graphics_get_event(&event)) {
        switch (event.type) {
            case GRAPHICS_EVENT_QUIT:
                return false;
            case GRAPHICS_EVENT_REFRESH:
                system->needs_redraw = true;
                break;
            case GRAPHICS_EVENT_FULLSCREEN_TOGGLE:
                window_set_fullscreen(system->window, !system->fullscreen);
                system->fullscreen = !system->fullscreen;
                system->needs_redraw = true;
                break;
            case GRAPHICS_EVENT_NONE:
            case GRAPHICS_EVENT_KEY_PRESS:
            default:
                break;
        }
    }

    bool window_resized = window_was_resized();
    bool needs_full_render = false;

    if (window_resized || system->window_size_dirty) {
        window_get_size(system->window, &system->cached_window_width, &system->cached_window_height);
        system->window_size_dirty = false;
        system->needs_redraw = true;
        needs_full_render = true;
    }

    int32_t current_plot_width = system->cached_window_width - (system->config->window_margin * 2);
    if (system->last_plot_width != current_plot_width) {
        uint32_t new_buffer_size = current_plot_width - 2;
        if (new_buffer_size > 0) {
            for (uint32_t i = 0; i < system->plot_count; i++) {
                if (system->plots[i].data_buffer) {
                    ringbuf_resize(system->plots[i].data_buffer, new_buffer_size);
                }
            }
        }
        system->last_plot_width = current_plot_width;
        system->needs_redraw = true;
        needs_full_render = true;
    }

    if (!plot_system_needs_redraw(system) && !needs_full_render) {
        return true;
    }

    int32_t plot_height = system->config->default_height;
    int32_t margin = system->config->window_margin;
    int32_t plot_spacing = 10;

    renderer_clear(system->renderer, system->config->background_color);

    for (uint32_t i = 0; i < system->plot_count; i++) {
        int32_t y = i * (plot_height + plot_spacing) + margin;
        plot_draw(&system->plots[i], system->renderer, system->font,
                  margin, y, current_plot_width, plot_height, system->config);
    }

    graphics_draw_fps_counter(system->renderer, system->font, system->config->fps_counter);

    renderer_present(system->renderer);

    system->needs_redraw = false;

    return true;
}