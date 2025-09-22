#ifndef DATASOURCE_H
#define DATASOURCE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool (*init)(const char *target, void **context);
    bool (*collect)(void *context, double *value);
    bool (*collect_dual)(void *context, double *value1, double *value2); // Optional: for dual-line plots
    void (*cleanup)(void *context);
    const char *name;
    const char *unit;
    bool is_dual; // Indicates if this datasource provides dual values
} datasource_handler_t;

typedef struct {
    datasource_handler_t *handler;
    void *context;
    char *target;
} datasource_t;

datasource_t *datasource_create(const char *type, const char *target);
bool datasource_collect(datasource_t *ds, double *value);
void datasource_destroy(datasource_t *ds);

#endif