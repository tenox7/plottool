#include "compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "platform.h"
#include "graphics.h"
#include "config.h"
#include "plot.h"
#include "ringbuf.h"
#include "threading.h"

static int running = 1;

void signal_handler(int sig) {
    exit(0);
}


int main(int argc, char *argv[]) {
    char *config_file = "plottool.ini";
    int i;
    uint32_t frame_count = 0;
    config_t *config;
    plot_system_t *plot_system;
    data_collector_t *data_collector;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            config_file = argv[i + 1];
            i++;
        } else {
            fprintf(stderr, "Usage: %s [-f config_file]\n", argv[0]);
            return 1;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (!platform_init()) {
        fprintf(stderr, "Failed to initialize platform\n");
        return 1;
    }

    if (!graphics_init()) {
        fprintf(stderr, "Failed to initialize graphics\n");
        platform_cleanup();
        return 1;
    }

    config = config_load(config_file);
    if (!config) {
        fprintf(stderr, "Failed to load configuration\n");
        graphics_cleanup();
        platform_cleanup();
        return 1;
    }
    
    
    plot_system = plot_system_create(config);
    if (!plot_system) {
        fprintf(stderr, "Failed to create plot system\n");
        config_destroy(config);
        graphics_cleanup();
        platform_cleanup();
        return 1;
    }
    
    data_collector = data_collector_create(config);
    if (!data_collector) {
        fprintf(stderr, "Failed to create data collector\n");
        plot_system_destroy(plot_system);
        config_destroy(config);
        graphics_cleanup();
        platform_cleanup();
        return 1;
    }
    
    plot_system_connect_data_buffers(plot_system, data_collector);
    
    if (!data_collector_start(data_collector)) {
        fprintf(stderr, "Failed to start data collector\n");
        data_collector_destroy(data_collector);
        plot_system_destroy(plot_system);
        config_destroy(config);
        graphics_cleanup();
        platform_cleanup();
        return 1;
    }
    

    graphics_start_render_timer(config->max_fps);

    while (running) {
        if (!plot_system_update(plot_system)) {
            exit(0);
        }

        frame_count++;
        if (frame_count % 60 == 0) {
        }

    }

    graphics_stop_render_timer();
    graphics_cleanup();
    platform_cleanup();

    exit(0);
    
    return 0;
}
