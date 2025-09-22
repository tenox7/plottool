/*
 * Ping datasource wrapper around sryze-ping library
 *
 * Original sryze-ping library by Sergey Zolotarev
 * https://github.com/sryze/ping
 * Licensed under MIT License (see sryze-ping.c for full text)
 */

#include "../datasource.h"
#include "sryze-ping.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    sryze_ping_context_t *sryze_ctx;
    char *target;
} ping_context_t;

static bool ping_init(const char *target, void **context) {
    if (!target) return false;

    ping_context_t *ctx = malloc(sizeof(ping_context_t));
    if (!ctx) return false;

    ctx->target = malloc(strlen(target) + 1);
    if (!ctx->target) {
        free(ctx);
        return false;
    }
    strcpy(ctx->target, target);

    ctx->sryze_ctx = sryze_ping_create(target, 1000);
    if (!ctx->sryze_ctx) {
        printf("Ping: Raw sockets unavailable for %s (requires root privileges)\n", target);
        free(ctx->target);
        free(ctx);
        return false;  // Fail initialization if raw sockets unavailable
    }

    *context = ctx;
    return true;
}

static bool ping_collect(void *context, double *value) {
    ping_context_t *ctx = (ping_context_t *)context;
    if (!ctx || !value) return false;

    double ping_time;
    bool success = sryze_ping_send(ctx->sryze_ctx, &ping_time);

    *value = success ? ping_time : -1.0;
    return success;
}

static void ping_cleanup(void *context) {
    ping_context_t *ctx = (ping_context_t *)context;
    if (!ctx) return;

    if (ctx->sryze_ctx) {
        sryze_ping_destroy(ctx->sryze_ctx);
    }
    free(ctx->target);
    free(ctx);
}

datasource_handler_t ping_handler = {
    .init = ping_init,
    .collect = ping_collect,
    .collect_dual = NULL,
    .cleanup = ping_cleanup,
    .name = "ping",
    .unit = "ms",
    .is_dual = false
};