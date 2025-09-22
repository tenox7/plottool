#include "../datasource.h"
#include "../platform.h"
#include <stddef.h>

static bool cpu_init(const char *target, void **context) {
    *context = NULL;
    return true;
}

static bool cpu_collect(void *context, double *value) {
    if (!value) return false;

    system_stats_t stats;
    if (!get_system_stats(&stats)) return false;

    *value = stats.cpu_usage;
    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    return true;
}

static bool cpu_collect_dual(void *context, double *total_value, double *system_value) {
    if (!total_value || !system_value) return false;

    system_stats_t stats;
    if (!get_system_stats(&stats)) return false;

    *total_value = stats.cpu_usage;
    if (*total_value > 100.0) *total_value = 100.0;
    if (*total_value < 0.0) *total_value = 0.0;

    *system_value = stats.system_cpu_usage;
    if (*system_value > 100.0) *system_value = 100.0;
    if (*system_value < 0.0) *system_value = 0.0;

    return true;
}

static void cpu_cleanup(void *context) {
}

datasource_handler_t cpu_handler = {
    .init = cpu_init,
    .collect = cpu_collect,
    .collect_dual = cpu_collect_dual,
    .cleanup = cpu_cleanup,
    .name = "cpu",
    .unit = "%",
    .is_dual = true
};