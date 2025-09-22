#include "../datasource.h"
#include <math.h>
#include <stdlib.h>

typedef struct {
    double t;
} sine_context_t;

static bool sine_init(const char *target, void **context) {
    sine_context_t *ctx = malloc(sizeof(sine_context_t));
    if (!ctx) return false;

    ctx->t = 0.0;
    *context = ctx;

    // Future: check target to determine test type (sine, cosine, square, etc.)
    // For now, only "sine" is supported
    return true;
}

static bool sine_collect(void *context, double *value) {
    sine_context_t *ctx = (sine_context_t *)context;
    if (!ctx || !value) return false;

    *value = sin(ctx->t) * 50.0 + 50.0;  // Sine wave centered at 50, amplitude 50
    ctx->t += 0.1;

    return true;
}

static bool sine_collect_dual(void *context, double *sine_value, double *cosine_value) {
    sine_context_t *ctx = (sine_context_t *)context;
    if (!ctx || !sine_value || !cosine_value) return false;

    *sine_value = sin(ctx->t) * 40.0 + 50.0;      // Sine wave (filled) - range 10 to 90
    *cosine_value = cos(ctx->t) * 35.0 + 50.0;    // Cosine wave (line) - range 15 to 85, smaller amplitude
    ctx->t += 0.1;

    return true;
}

static void sine_cleanup(void *context) {
    free(context);
}

datasource_handler_t sine_handler = {
    .init = sine_init,
    .collect = sine_collect,
    .collect_dual = sine_collect_dual,
    .cleanup = sine_cleanup,
    .name = "test",
    .unit = "",
    .is_dual = true
};