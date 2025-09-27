#define _GNU_SOURCE
#include "config.h"
#include "ini_parser.h"
#include "default_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static config_t *global_config = NULL;

static char *create_temp_config_file(void) {
    static char temp_path[256];
    FILE *f;

#ifdef _WIN32
    char temp_dir[256];
    DWORD len = GetTempPathA(sizeof(temp_dir), temp_dir);
    if (len == 0) {
        strcpy(temp_dir, "C:\\TEMP\\");
    }

    if (GetTempFileNameA(temp_dir, "plt", 0, temp_path) == 0) {
        return NULL;
    }

    f = fopen(temp_path, "w");
    if (!f) return NULL;

#else
    const char *tmp_dir = getenv("TMPDIR");
    if (!tmp_dir) tmp_dir = "/tmp";

    sprintf(temp_path, "%s/plottool_config_XXXXXX", tmp_dir);

    int fd = mkstemp(temp_path);
    if (fd == -1) return NULL;

    f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        unlink(temp_path);
        return NULL;
    }
#endif

    fprintf(f, "%s", DEFAULT_CONFIG_INI);
    fclose(f);

    return temp_path;
}

static color_t parse_color(const char *str) {
    color_t color = {0, 0, 0, 255};
    if (!str || strlen(str) != 6) return color;
    
    unsigned int r, g, b;
    if (sscanf(str, "%2x%2x%2x", &r, &g, &b) == 3) {
        color.r = (uint8_t)r;
        color.g = (uint8_t)g;
        color.b = (uint8_t)b;
    }
    return color;
}

static bool parse_type_target(const char *type, const char *target, plot_config_t *plot, config_t *config) {
    if (!type || !target) return false;

    // Route bw targets to appropriate handlers
    const char *actual_type = type;
    const char *actual_target = target;

    if (strcmp(type, "bw") == 0) {
        if (strncmp(target, "snmp1,", 6) == 0) {
            // Route bw=snmp1,host,community,interface to snmp handler
            actual_type = "snmp";
            actual_target = target + 6; // Skip "snmp1," prefix
        } else {
            // Route bw=local,interface to if_thr handler
            actual_type = "if_thr";
        }
    }

    // Auto-generate plot name: "TYPE - target"
    char auto_name[256];

    // Convert type to uppercase (use original type for display)
    char type_upper[64];
    for (int i = 0; type[i] && i < 63; i++) {
        type_upper[i] = (type[i] >= 'a' && type[i] <= 'z') ? type[i] - 'a' + 'A' : type[i];
    }
    type_upper[strlen(type)] = '\0';

    // Special formatting for SNMP to make it cleaner
    if (strcmp(actual_type, "snmp") == 0) {
        // Parse SNMP target: "host,community,interface" -> "host:interface"
        char host[128], community[64], interface[32];
        if (sscanf(actual_target, "%127[^,],%63[^,],%31s", host, community, interface) == 3) {
            snprintf(auto_name, sizeof(auto_name), "BW - %s:%s", host, interface);
        } else {
            snprintf(auto_name, sizeof(auto_name), "BW - %s", actual_target);
        }
    } else if (strcmp(type, "bw") == 0) {
        // For bw=local,interface format
        snprintf(auto_name, sizeof(auto_name), "BW - %s", actual_target);
    } else {
        snprintf(auto_name, sizeof(auto_name), "%s - %s", type_upper, actual_target);
    }

    printf("Parsing target: target=%s, type=%s, auto_name=%s\n", actual_target, actual_type, auto_name);

    plot->type = malloc(strlen(actual_type) + 1);
    plot->target = malloc(strlen(actual_target) + 1);
    plot->name = malloc(strlen(auto_name) + 1);

    if (!plot->type || !plot->target || !plot->name) {
        free(plot->type);
        free(plot->target);
        free(plot->name);
        return false;
    }

    strcpy(plot->type, actual_type);
    strcpy(plot->target, actual_target);
    strcpy(plot->name, auto_name);

    plot->line_color = config->line_color;
    plot->line_color_secondary = config->line_color_secondary;

    plot->background_color = (color_t){100, 100, 100, 255};
    plot->height = 100;
    plot->refresh_interval_ms = 0;

    return true;
}

static void parse_plot_config(ini_file_t *ini, plot_config_t *plot, const char *section_name) {
    char *value;
    
    if ((value = ini_get_value(ini, section_name, "line_color"))) {
        plot->line_color = parse_color(value);
    }

    if ((value = ini_get_value(ini, section_name, "line_color_secondary"))) {
        plot->line_color_secondary = parse_color(value);
    }

    if ((value = ini_get_value(ini, section_name, "background_color"))) {
        plot->background_color = parse_color(value);
    }

    if ((value = ini_get_value(ini, section_name, "height"))) {
        plot->height = atoi(value);
    }

    if ((value = ini_get_value(ini, section_name, "refresh_interval_sec"))) {
        plot->refresh_interval_ms = atoi(value) * 1000;
    }
}

static bool is_config_valid(ini_file_t *ini) {
    if (!ini || ini->section_count == 0) return false;

    for (int i = 0; i < ini->section_count; i++) {
        if (strcmp(ini->sections[i].section, "global") == 0) {
            return true;
        }
    }
    return false;
}

config_t *config_load(const char *filename) {
    ini_file_t *ini = ini_parse_file(filename);
    char *temp_config_path = NULL;
    bool use_defaults = false;

    if (!ini) {
        printf("Config file %s not found, using defaults\n", filename);
        use_defaults = true;
    } else if (!is_config_valid(ini)) {
        printf("Config file %s invalid or corrupt, using defaults\n", filename);
        ini_free(ini);
        ini = NULL;
        use_defaults = true;
    }

    if (use_defaults) {
        temp_config_path = create_temp_config_file();
        if (!temp_config_path) {
            fprintf(stderr, "Could not create temporary config file\n");
            return NULL;
        }
        ini = ini_parse_file(temp_config_path);
        if (!ini) {
            fprintf(stderr, "Could not parse temporary config file\n");
#ifdef _WIN32
            DeleteFileA(temp_config_path);
#else
            unlink(temp_config_path);
#endif
            return NULL;
        }
    }
    
    config_t *config = malloc(sizeof(config_t));
    if (!config) {
        ini_free(ini);
        if (temp_config_path) {
#ifdef _WIN32
            DeleteFileA(temp_config_path);
#else
            unlink(temp_config_path);
#endif
        }
        return NULL;
    }
    
    config->background_color = (color_t){100, 100, 100, 255};
    config->text_color = (color_t){255, 255, 255, 255};
    config->border_color = (color_t){255, 255, 255, 255};
    config->line_color = (color_t){0, 255, 0, 255};
    config->line_color_secondary = (color_t){0, 0, 255, 255};
    config->default_height = 100;
    config->default_width = 400;
    config->refresh_interval_ms = 1000;
    config->window_margin = 5;
    config->max_fps = 30;
    config->fullscreen = FULLSCREEN_OFF;
    config->fps_counter = false;
    config->plots = NULL;
    config->plot_count = 0;
    
    char *value;
    
    if ((value = ini_get_value(ini, "global", "background_color"))) {
        config->background_color = parse_color(value);
    }
    if ((value = ini_get_value(ini, "global", "text_color"))) {
        config->text_color = parse_color(value);
    }
    if ((value = ini_get_value(ini, "global", "border_color"))) {
        config->border_color = parse_color(value);
    }
    if ((value = ini_get_value(ini, "global", "line_color"))) {
        config->line_color = parse_color(value);
    }
    if ((value = ini_get_value(ini, "global", "line_color_secondary"))) {
        config->line_color_secondary = parse_color(value);
    }
    if ((value = ini_get_value(ini, "global", "default_height"))) {
        config->default_height = atoi(value);
    }
    if ((value = ini_get_value(ini, "global", "default_width"))) {
        config->default_width = atoi(value);
    }
    if ((value = ini_get_value(ini, "global", "refresh_interval_sec"))) {
        config->refresh_interval_ms = atoi(value) * 1000;
    }
    if ((value = ini_get_value(ini, "global", "window_margin"))) {
        config->window_margin = atoi(value);
    }
    if ((value = ini_get_value(ini, "global", "max_fps"))) {
        config->max_fps = atoi(value);
    }
    if ((value = ini_get_value(ini, "global", "fullscreen"))) {
        if (strcmp(value, "force") == 0) {
            config->fullscreen = FULLSCREEN_FORCE;
        } else if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
            config->fullscreen = FULLSCREEN_ON;
        } else {
            config->fullscreen = FULLSCREEN_OFF;
        }
    }
    if ((value = ini_get_value(ini, "global", "fps_counter"))) {
        config->fps_counter = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    }

    plot_config_t *plots = NULL;
    uint32_t plot_count = 0;
    uint32_t plot_capacity = 0;
    
    // First pass: parse targets section for basic plot definitions
    for (int i = 0; i < ini->section_count; i++) {
        if (strcmp(ini->sections[i].section, "targets") == 0) {
            for (int j = 0; j < ini->sections[i].pair_count; j++) {
                if (plot_count >= plot_capacity) {
                    plot_capacity = plot_capacity ? plot_capacity * 2 : 4;
                    plots = realloc(plots, sizeof(plot_config_t) * plot_capacity);
                    if (!plots) {
                        ini_free(ini);
                        free(config);
                        if (temp_config_path) {
#ifdef _WIN32
                            DeleteFileA(temp_config_path);
#else
                            unlink(temp_config_path);
#endif
                        }
                        return NULL;
                    }
                }
                
                char *type = ini->sections[i].pairs[j].key;
                char *target = ini->sections[i].pairs[j].value;

                if (parse_type_target(type, target, &plots[plot_count], config)) {
                    plot_count++;
                }
            }
            break;
        }
    }
    
    // Second pass: look for individual plot sections to override defaults
    for (int i = 0; i < ini->section_count; i++) {
        char *section_name = ini->sections[i].section;
        
        if (strcmp(section_name, "global") == 0 || strcmp(section_name, "targets") == 0) {
            continue;
        }
        
        for (uint32_t j = 0; j < plot_count; j++) {
            if (strcmp(plots[j].name, section_name) == 0) {
                parse_plot_config(ini, &plots[j], section_name);
                break;
            }
        }
    }
    
    config->plots = plots;
    config->plot_count = plot_count;

    global_config = config;

    ini_free(ini);

    if (temp_config_path) {
#ifdef _WIN32
        DeleteFileA(temp_config_path);
#else
        unlink(temp_config_path);
#endif
    }

    return config;
}

void config_destroy(config_t *config) {
    if (!config) return;

    if (global_config == config) {
        global_config = NULL;
    }

    for (uint32_t i = 0; i < config->plot_count; i++) {
        free(config->plots[i].name);
        free(config->plots[i].type);
        free(config->plots[i].target);
    }

    free(config->plots);
    free(config);
}

int config_get_max_fps(void) {
    if (!global_config) return 2;
    return global_config->max_fps;
}
