#include "../datasource.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/host_info.h>
#include <mach/mach_host.h>
#include <mach/vm_statistics.h>
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#if defined(__NetBSD__) || defined(__OpenBSD__)
#include <uvm/uvm_extern.h>
#endif

#if defined(__unix__) || defined(__unix) || defined(unix)
#include <stdio.h>
#endif

typedef struct {
    double min;
    double max;
    uint64_t sum;
    uint32_t sample_count;
    double last;
} memory_stats_t;

static int memory_init(const char *target, void **context) {
    memory_stats_t *ctx = malloc(sizeof(memory_stats_t));
    if (!ctx) return 0;

    ctx->min = 100.0;
    ctx->max = 0.0;
    ctx->sum = 0;
    ctx->sample_count = 0;
    ctx->last = 0.0;

    *context = ctx;
    return 1;
}

static int memory_collect(void *context, double *value) {
    memory_stats_t *ctx = (memory_stats_t *)context;
    if (!ctx || !value) return 0;

    uint64_t total_memory = 0;
    uint64_t free_memory = 0;

#ifdef __APPLE__
    int mib[2];
    size_t len;
    vm_statistics64_data_t vm_stats;
    mach_msg_type_number_t count;

    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    len = sizeof(total_memory);
    if (sysctl(mib, 2, &total_memory, &len, NULL, 0) != 0) {
        return 0;
    }

    count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                         (host_info64_t)&vm_stats, &count) != KERN_SUCCESS) {
        return 0;
    }

    free_memory = vm_stats.free_count * 4096;

#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    size_t size;
    uint64_t free_pages, page_size;

    size = sizeof(uint64_t);
    if (sysctlbyname("hw.physmem", &total_memory, &size, NULL, 0) != 0) {
        total_memory = 0;
    }

#ifdef __FreeBSD__
    free_pages = 0;
    page_size = 0;
    size = sizeof(free_pages);
    if (sysctlbyname("vm.stats.vm.v_free_count", &free_pages, &size, NULL, 0) == 0) {
        size = sizeof(page_size);
        if (sysctlbyname("vm.stats.vm.v_page_size", &page_size, &size, NULL, 0) == 0) {
            free_memory = free_pages * page_size;
        }
    }
#elif defined(__NetBSD__) || defined(__OpenBSD__)
    struct uvmexp uvmexp;
    size = sizeof(uvmexp);
    if (sysctlbyname("vm.uvmexp", &uvmexp, &size, NULL, 0) == 0) {
        free_memory = uvmexp.free * uvmexp.pagesize;
    }
#endif

#elif defined(__unix__) || defined(__unix) || defined(unix)
    FILE *fp;
    char line[256];
    uint64_t mem_total, mem_available;

    fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0;

    mem_total = 0;
    mem_available = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "MemTotal: %lu kB", &mem_total) == 1) {
            total_memory = mem_total * 1024;
        } else if (sscanf(line, "MemAvailable: %lu kB", &mem_available) == 1) {
            free_memory = mem_available * 1024;
        }
    }
    fclose(fp);

#else
    return 0;
#endif

    if (total_memory == 0) return 0;

    uint64_t used_memory = total_memory - free_memory;
    *value = (double)used_memory / (double)total_memory * 100.0;

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    if (*value < ctx->min) ctx->min = *value;
    if (*value > ctx->max) ctx->max = *value;
    ctx->sum += (uint64_t)*value;
    ctx->last = *value;
    ctx->sample_count++;

    return 1;
}

static int memory_get_stats(void *context, datasource_stats_t *stats) {
    memory_stats_t *ctx = (memory_stats_t *)context;
    if (!ctx || !stats) return 0;

    if (ctx->sample_count == 0) {
        stats->min = 0.0;
        stats->max = 0.0;
        stats->avg = 0.0;
        stats->last = 0.0;
        stats->min_secondary = 0.0;
        stats->max_secondary = 0.0;
        stats->avg_secondary = 0.0;
        stats->last_secondary = 0.0;
        return 1;
    }

    stats->min = ctx->min;
    stats->max = ctx->max;
    stats->avg = (double)(ctx->sum / ctx->sample_count);
    stats->last = ctx->last;
    stats->min_secondary = 0.0;
    stats->max_secondary = 0.0;
    stats->avg_secondary = 0.0;
    stats->last_secondary = 0.0;

    return 1;
}

static void memory_cleanup(void *context) {
    free(context);
}

static void memory_format_value(double value, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%.1f%%", value);
}

datasource_handler_t memory_handler = {
    .init = memory_init,
    .collect = memory_collect,
    .collect_dual = NULL,
    .get_stats = memory_get_stats,
    .format_value = memory_format_value,
    .cleanup = memory_cleanup,
    .name = "memory",
    .unit = "%",
    .is_dual = 0,
    .max_scale = 100.0
};
