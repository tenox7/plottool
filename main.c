#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdatomic.h>

#include "platform.h"
#include "graphics.h"
#include "config.h"
#include "plot.h"
#include "ringbuf.h"
#include "threading.h"

static int running = 1;

void signal_handler(int sig) {
    printf("Received signal %d, exiting immediately...\n", sig);
    exit(0);
}


int main(int argc, char *argv[]) {
    char *config_file = "plottool.ini";

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            config_file = argv[i + 1];
            i++; // Skip the next argument since it's the config file name
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

    config_t *config = config_load(config_file);
    if (!config) {
        fprintf(stderr, "Failed to load configuration\n");
        graphics_cleanup();
        platform_cleanup();
        return 1;
    }
    
    printf("Loaded %u plots from configuration\n", config->plot_count);
    
    plot_system_t *plot_system = plot_system_create(config);
    if (!plot_system) {
        fprintf(stderr, "Failed to create plot system\n");
        config_destroy(config);
        graphics_cleanup();
        platform_cleanup();
        return 1;
    }
    printf("Created plot system with %u plots\n", plot_system->plot_count);
    
    data_collector_t *data_collector = data_collector_create(config);
    if (!data_collector) {
        fprintf(stderr, "Failed to create data collector\n");
        plot_system_destroy(plot_system);
        config_destroy(config);
        graphics_cleanup();
        platform_cleanup();
        return 1;
    }
    printf("Created data collector with %u sources\n", data_collector->source_count);
    
    plot_system_connect_data_buffers(plot_system, data_collector);
    printf("Connected data buffers to plot system\n");
    
    if (!data_collector_start(data_collector)) {
        fprintf(stderr, "Failed to start data collector\n");
        data_collector_destroy(data_collector);
        plot_system_destroy(plot_system);
        config_destroy(config);
        graphics_cleanup();
        platform_cleanup();
        return 1;
    }
    printf("Started data collection threads\n");
    
    printf("Starting main render loop...\n");
    uint32_t frame_count = 0;

    // Start the render timer for event-driven rendering
    graphics_start_render_timer(config->max_fps);

    while (running) {
        if (!plot_system_update(plot_system)) {
            printf("Window closed, exiting immediately...\n");
            exit(0);
        }

        frame_count++;
        if (frame_count % 60 == 0) {
            printf("Rendered %u frames\n", frame_count);
        }

        // No manual sleep needed - SDL_WaitEvent() blocks until timer or user event
    }
    printf("Render loop ended after %u frames\n", frame_count);

    graphics_stop_render_timer();
    graphics_cleanup();
    platform_cleanup();

    exit(0);
    
    return 0;
}