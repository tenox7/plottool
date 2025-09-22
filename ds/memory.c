#include "../datasource.h"
#include "../platform.h"
#include <stddef.h>

static bool memory_init(const char *target, void **context) {
    *context = NULL;
    return true;
}

static bool memory_collect(void *context, double *value) {
    if (!value) return false;

    system_stats_t stats;
    if (!get_system_stats(&stats)) return false;

    if (stats.total_memory == 0) return false;

    uint64_t used_memory = stats.total_memory - stats.free_memory;
    *value = (double)used_memory / (double)stats.total_memory * 100.0;

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    return true;
}

static void memory_cleanup(void *context) {
}

datasource_handler_t memory_handler = {
    .init = memory_init,
    .collect = memory_collect,
    .collect_dual = NULL,
    .cleanup = memory_cleanup,
    .name = "memory",
    .unit = "%",
    .is_dual = false
};