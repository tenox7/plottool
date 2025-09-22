#define _GNU_SOURCE
#include "datasource.h"
#include <stdlib.h>
#include <string.h>

extern datasource_handler_t ping_handler;
extern datasource_handler_t cpu_handler;
extern datasource_handler_t memory_handler;
extern datasource_handler_t sine_handler;
extern datasource_handler_t snmp_handler;
extern datasource_handler_t if_thr_handler;

static datasource_handler_t *handlers[] = {
    &ping_handler,
    &cpu_handler,
    &memory_handler,
    &sine_handler,
    &snmp_handler,
    &if_thr_handler,
    NULL
};

datasource_t *datasource_create(const char *type, const char *target) {
    if (!type) return NULL;

    datasource_handler_t *handler = NULL;
    for (int i = 0; handlers[i]; i++) {
        if (strcmp(handlers[i]->name, type) == 0) {
            handler = handlers[i];
            break;
        }
    }

    if (!handler) return NULL;

    datasource_t *ds = malloc(sizeof(datasource_t));
    if (!ds) return NULL;

    ds->handler = handler;
    ds->target = target ? strdup(target) : NULL;

    if (!handler->init(target, &ds->context)) {
        free(ds->target);
        free(ds);
        return NULL;
    }

    return ds;
}

bool datasource_collect(datasource_t *ds, double *value) {
    if (!ds || !ds->handler) return false;
    return ds->handler->collect(ds->context, value);
}

void datasource_destroy(datasource_t *ds) {
    if (!ds) return;

    if (ds->handler && ds->handler->cleanup) {
        ds->handler->cleanup(ds->context);
    }

    free(ds->target);
    free(ds);
}