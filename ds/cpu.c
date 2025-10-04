#include "../datasource.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#if defined(__sun__) || defined(__sun) || defined(sun)
#include <kstat.h>
#include <unistd.h>
#include <string.h>
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/host_info.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#if defined(__unix__) || defined(__unix) || defined(unix)
#endif

typedef struct {
    double min_total;
    double max_total;
    double min_system;
    double max_system;
    uint64_t sum_total;
    uint64_t sum_system;
    uint32_t sample_count;
    double last_total;
    double last_system;
} cpu_stats_t;

static int cpu_init(const char *target, void **context) {
    cpu_stats_t *ctx = malloc(sizeof(cpu_stats_t));
    if (!ctx) return 0;

    ctx->min_total = 100.0;
    ctx->max_total = 0.0;
    ctx->min_system = 100.0;
    ctx->max_system = 0.0;
    ctx->sum_total = 0;
    ctx->sum_system = 0;
    ctx->sample_count = 0;
    ctx->last_total = 0.0;
    ctx->last_system = 0.0;

    *context = ctx;
    return 1;
}

static int cpu_collect(void *context, double *value) {
    cpu_stats_t *ctx = (cpu_stats_t *)context;
    if (!ctx || !value) return 0;

#if defined(__sun__) || defined(__sun) || defined(sun)
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;
    static kstat_ctl_t *kc = NULL;

    if (!kc) {
        kc = kstat_open();
        if (!kc) return 0;
    }

    kstat_t *ksp;
    kstat_named_t *kn;
    uint64_t user = 0, sys = 0, idle = 0;
    uint64_t total_ticks;
    uint64_t idle_diff, total_diff;

    for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next) {
        if (strcmp(ksp->ks_module, "cpu_stat") == 0) {
            if (kstat_read(kc, ksp, NULL) == -1) continue;

            kn = (kstat_named_t *)kstat_data_lookup(ksp, "cpu_ticks_user");
            if (kn) user += kn->value.ul;

            kn = (kstat_named_t *)kstat_data_lookup(ksp, "cpu_ticks_kernel");
            if (kn) sys += kn->value.ul;

            kn = (kstat_named_t *)kstat_data_lookup(ksp, "cpu_ticks_idle");
            if (kn) idle += kn->value.ul;
        }
    }

    total_ticks = user + sys + idle;

    if (prev_total != 0 && total_ticks > prev_total) {
        idle_diff = idle - prev_idle;
        total_diff = total_ticks - prev_total;
        *value = 100.0 * (1.0 - (double)idle_diff / total_diff);
    } else {
        *value = 0.0;
    }

    prev_idle = idle;
    prev_total = total_ticks;

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    if (*value < ctx->min_total) ctx->min_total = *value;
    if (*value > ctx->max_total) ctx->max_total = *value;
    ctx->sum_total += (uint64_t)*value;
    ctx->last_total = *value;
    ctx->sample_count++;

    return 1;
#elif defined(__APPLE__)
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;

    processor_info_array_t cpu_info;
    mach_msg_type_number_t num_cpu_info;
    natural_t num_processors;
    uint64_t idle, total;
    natural_t i;
    processor_cpu_load_info_t cpu_load;
    uint64_t idle_diff, total_diff;

    if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
                           &num_processors, &cpu_info, &num_cpu_info) != KERN_SUCCESS) {
        return 0;
    }

    idle = 0;
    total = 0;
    for (i = 0; i < num_processors; i++) {
        cpu_load = (processor_cpu_load_info_t)cpu_info;
        idle += cpu_load[i].cpu_ticks[CPU_STATE_IDLE];
        total += cpu_load[i].cpu_ticks[CPU_STATE_IDLE] +
                cpu_load[i].cpu_ticks[CPU_STATE_USER] +
                cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM] +
                cpu_load[i].cpu_ticks[CPU_STATE_NICE];
    }

    if (prev_total != 0) {
        idle_diff = idle - prev_idle;
        total_diff = total - prev_total;
        *value = 100.0 * (1.0 - (double)idle_diff / total_diff);
    } else {
        *value = 0.0;
    }

    prev_idle = idle;
    prev_total = total;

    vm_deallocate(mach_task_self(), (vm_address_t)cpu_info,
                  num_cpu_info * sizeof(integer_t));

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    if (*value < ctx->min_total) ctx->min_total = *value;
    if (*value > ctx->max_total) ctx->max_total = *value;
    ctx->sum_total += (uint64_t)*value;
    ctx->last_total = *value;
    ctx->sample_count++;

    return 1;
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;

    size_t size;
    long cp_time[5];
    uint64_t user, nice, system, interrupt, idle;
    uint64_t total_ticks;
    uint64_t idle_diff, total_diff;
    uint64_t cp_time_64[5];

#ifdef __FreeBSD__
    size = sizeof(cp_time);
    if (sysctlbyname("kern.cp_time", cp_time, &size, NULL, 0) == 0) {
        user = cp_time[0];
        nice = cp_time[1];
        system = cp_time[2];
        interrupt = cp_time[3];
        idle = cp_time[4];

        total_ticks = user + nice + system + interrupt + idle;

        if (prev_total != 0) {
            idle_diff = idle - prev_idle;
            total_diff = total_ticks - prev_total;

            if (total_diff > 0) {
                *value = 100.0 * (1.0 - (double)idle_diff / total_diff);
            } else {
                *value = 0.0;
            }
        } else {
            *value = 0.0;
        }

        prev_idle = idle;
        prev_total = total_ticks;
    }
#elif defined(__NetBSD__) || defined(__OpenBSD__)
    size = sizeof(cp_time_64);
    if (sysctlbyname("kern.cp_time", cp_time_64, &size, NULL, 0) == 0) {
        user = cp_time_64[0];
        nice = cp_time_64[1];
        system = cp_time_64[2];
        interrupt = cp_time_64[3];
        idle = cp_time_64[4];

        total_ticks = user + nice + system + interrupt + idle;

        if (prev_total != 0) {
            idle_diff = idle - prev_idle;
            total_diff = total_ticks - prev_total;

            if (total_diff > 0) {
                *value = 100.0 * (1.0 - (double)idle_diff / total_diff);
            } else {
                *value = 0.0;
            }
        } else {
            *value = 0.0;
        }

        prev_idle = idle;
        prev_total = total_ticks;
    }
#endif

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    if (*value < ctx->min_total) ctx->min_total = *value;
    if (*value > ctx->max_total) ctx->max_total = *value;
    ctx->sum_total += (uint64_t)*value;
    ctx->last_total = *value;
    ctx->sample_count++;

    return 1;
#elif defined(__unix__) || defined(__unix) || defined(unix)
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;

    FILE *fp;
    char line[256];
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
    uint64_t total_ticks;
    uint64_t idle_diff, total_diff;

    fp = fopen("/proc/stat", "r");
    if (!fp) return 0;

    if (fgets(line, sizeof(line), fp) &&
        sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 8) {

        total_ticks = user + nice + system + idle + iowait + irq + softirq + steal;

        if (prev_total != 0) {
            idle_diff = idle - prev_idle;
            total_diff = total_ticks - prev_total;

            if (total_diff > 0) {
                *value = 100.0 * (1.0 - (double)idle_diff / total_diff);
            } else {
                *value = 0.0;
            }
        } else {
            *value = 0.0;
        }

        prev_idle = idle;
        prev_total = total_ticks;
    }
    fclose(fp);

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;

    if (*value < ctx->min_total) ctx->min_total = *value;
    if (*value > ctx->max_total) ctx->max_total = *value;
    ctx->sum_total += (uint64_t)*value;
    ctx->last_total = *value;
    ctx->sample_count++;

    return 1;
#else
    return 0;
#endif
}

static int cpu_collect_dual(void *context, double *total_value, double *system_value) {
    cpu_stats_t *ctx = (cpu_stats_t *)context;
    if (!ctx || !total_value || !system_value) return 0;

#if defined(__sun__) || defined(__sun) || defined(sun)
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;
    static uint64_t prev_system = 0;
    static kstat_ctl_t *kc = NULL;

    if (!kc) {
        kc = kstat_open();
        if (!kc) return 0;
    }

    kstat_t *ksp;
    kstat_named_t *kn;
    uint64_t user = 0, sys = 0, idle = 0;
    uint64_t total_ticks, system_ticks;
    uint64_t idle_diff, total_diff, system_diff;

    for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next) {
        if (strcmp(ksp->ks_module, "cpu_stat") == 0) {
            if (kstat_read(kc, ksp, NULL) == -1) continue;

            kn = (kstat_named_t *)kstat_data_lookup(ksp, "cpu_ticks_user");
            if (kn) user += kn->value.ul;

            kn = (kstat_named_t *)kstat_data_lookup(ksp, "cpu_ticks_kernel");
            if (kn) sys += kn->value.ul;

            kn = (kstat_named_t *)kstat_data_lookup(ksp, "cpu_ticks_idle");
            if (kn) idle += kn->value.ul;
        }
    }

    total_ticks = user + sys + idle;
    system_ticks = sys;

    if (prev_total != 0 && total_ticks > prev_total) {
        idle_diff = idle - prev_idle;
        total_diff = total_ticks - prev_total;
        system_diff = system_ticks - prev_system;

        *total_value = 100.0 * (1.0 - (double)idle_diff / total_diff);
        *system_value = 100.0 * (double)system_diff / total_diff;
    } else {
        *total_value = 0.0;
        *system_value = 0.0;
    }

    prev_idle = idle;
    prev_total = total_ticks;
    prev_system = system_ticks;

    if (*total_value > 100.0) *total_value = 100.0;
    if (*total_value < 0.0) *total_value = 0.0;
    if (*system_value > 100.0) *system_value = 100.0;
    if (*system_value < 0.0) *system_value = 0.0;

    if (*total_value < ctx->min_total) ctx->min_total = *total_value;
    if (*total_value > ctx->max_total) ctx->max_total = *total_value;
    if (*system_value < ctx->min_system) ctx->min_system = *system_value;
    if (*system_value > ctx->max_system) ctx->max_system = *system_value;
    ctx->sum_total += (uint64_t)*total_value;
    ctx->sum_system += (uint64_t)*system_value;
    ctx->last_total = *total_value;
    ctx->last_system = *system_value;
    ctx->sample_count++;

    return 1;
#elif defined(__APPLE__)
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;
    static uint64_t prev_system = 0;

    processor_info_array_t cpu_info;
    mach_msg_type_number_t num_cpu_info;
    natural_t num_processors;
    uint64_t idle, total, system;
    natural_t i;
    processor_cpu_load_info_t cpu_load;
    uint64_t idle_diff, total_diff, system_diff;

    if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
                           &num_processors, &cpu_info, &num_cpu_info) != KERN_SUCCESS) {
        return 0;
    }

    idle = 0;
    total = 0;
    system = 0;
    for (i = 0; i < num_processors; i++) {
        cpu_load = (processor_cpu_load_info_t)cpu_info;
        idle += cpu_load[i].cpu_ticks[CPU_STATE_IDLE];
        system += cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM];
        total += cpu_load[i].cpu_ticks[CPU_STATE_IDLE] +
                cpu_load[i].cpu_ticks[CPU_STATE_USER] +
                cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM] +
                cpu_load[i].cpu_ticks[CPU_STATE_NICE];
    }

    if (prev_total != 0) {
        idle_diff = idle - prev_idle;
        total_diff = total - prev_total;
        system_diff = system - prev_system;

        *total_value = 100.0 * (1.0 - (double)idle_diff / total_diff);
        *system_value = 100.0 * (double)system_diff / total_diff;
    } else {
        *total_value = 0.0;
        *system_value = 0.0;
    }

    prev_idle = idle;
    prev_total = total;
    prev_system = system;

    vm_deallocate(mach_task_self(), (vm_address_t)cpu_info,
                  num_cpu_info * sizeof(integer_t));

    if (*total_value > 100.0) *total_value = 100.0;
    if (*total_value < 0.0) *total_value = 0.0;
    if (*system_value > 100.0) *system_value = 100.0;
    if (*system_value < 0.0) *system_value = 0.0;

    if (*total_value < ctx->min_total) ctx->min_total = *total_value;
    if (*total_value > ctx->max_total) ctx->max_total = *total_value;
    if (*system_value < ctx->min_system) ctx->min_system = *system_value;
    if (*system_value > ctx->max_system) ctx->max_system = *system_value;
    ctx->sum_total += (uint64_t)*total_value;
    ctx->sum_system += (uint64_t)*system_value;
    ctx->last_total = *total_value;
    ctx->last_system = *system_value;
    ctx->sample_count++;

    return 1;
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;
    static uint64_t prev_system = 0;

    size_t size;
    long cp_time[5];
    uint64_t user, nice, system, interrupt, idle;
    uint64_t total_ticks, system_ticks;
    uint64_t idle_diff, total_diff, system_diff;
    uint64_t cp_time_64[5];

#ifdef __FreeBSD__
    size = sizeof(cp_time);
    if (sysctlbyname("kern.cp_time", cp_time, &size, NULL, 0) == 0) {
        user = cp_time[0];
        nice = cp_time[1];
        system = cp_time[2];
        interrupt = cp_time[3];
        idle = cp_time[4];

        total_ticks = user + nice + system + interrupt + idle;
        system_ticks = system + interrupt;

        if (prev_total != 0) {
            idle_diff = idle - prev_idle;
            total_diff = total_ticks - prev_total;
            system_diff = system_ticks - prev_system;

            if (total_diff > 0) {
                *total_value = 100.0 * (1.0 - (double)idle_diff / total_diff);
                *system_value = 100.0 * (double)system_diff / total_diff;
            } else {
                *total_value = 0.0;
                *system_value = 0.0;
            }
        } else {
            *total_value = 0.0;
            *system_value = 0.0;
        }

        prev_idle = idle;
        prev_total = total_ticks;
        prev_system = system_ticks;
    }
#elif defined(__NetBSD__) || defined(__OpenBSD__)
    size = sizeof(cp_time_64);
    if (sysctlbyname("kern.cp_time", cp_time_64, &size, NULL, 0) == 0) {
        user = cp_time_64[0];
        nice = cp_time_64[1];
        system = cp_time_64[2];
        interrupt = cp_time_64[3];
        idle = cp_time_64[4];

        total_ticks = user + nice + system + interrupt + idle;
        system_ticks = system + interrupt;

        if (prev_total != 0) {
            idle_diff = idle - prev_idle;
            total_diff = total_ticks - prev_total;
            system_diff = system_ticks - prev_system;

            if (total_diff > 0) {
                *total_value = 100.0 * (1.0 - (double)idle_diff / total_diff);
                *system_value = 100.0 * (double)system_diff / total_diff;
            } else {
                *total_value = 0.0;
                *system_value = 0.0;
            }
        } else {
            *total_value = 0.0;
            *system_value = 0.0;
        }

        prev_idle = idle;
        prev_total = total_ticks;
        prev_system = system_ticks;
    }
#endif

    if (*total_value > 100.0) *total_value = 100.0;
    if (*total_value < 0.0) *total_value = 0.0;
    if (*system_value > 100.0) *system_value = 100.0;
    if (*system_value < 0.0) *system_value = 0.0;

    if (*total_value < ctx->min_total) ctx->min_total = *total_value;
    if (*total_value > ctx->max_total) ctx->max_total = *total_value;
    if (*system_value < ctx->min_system) ctx->min_system = *system_value;
    if (*system_value > ctx->max_system) ctx->max_system = *system_value;
    ctx->sum_total += (uint64_t)*total_value;
    ctx->sum_system += (uint64_t)*system_value;
    ctx->last_total = *total_value;
    ctx->last_system = *system_value;
    ctx->sample_count++;

    return 1;
#elif defined(__unix__) || defined(__unix) || defined(unix)
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;
    static uint64_t prev_system = 0;

    FILE *fp;
    char line[256];
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
    uint64_t total_ticks, system_ticks;
    uint64_t idle_diff, total_diff, system_diff;

    fp = fopen("/proc/stat", "r");
    if (!fp) return 0;

    if (fgets(line, sizeof(line), fp) &&
        sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 8) {

        total_ticks = user + nice + system + idle + iowait + irq + softirq + steal;
        system_ticks = system + irq + softirq;

        if (prev_total != 0) {
            idle_diff = idle - prev_idle;
            total_diff = total_ticks - prev_total;
            system_diff = system_ticks - prev_system;

            if (total_diff > 0) {
                *total_value = 100.0 * (1.0 - (double)idle_diff / total_diff);
                *system_value = 100.0 * (double)system_diff / total_diff;
            } else {
                *total_value = 0.0;
                *system_value = 0.0;
            }
        } else {
            *total_value = 0.0;
            *system_value = 0.0;
        }

        prev_idle = idle;
        prev_total = total_ticks;
        prev_system = system_ticks;
    }
    fclose(fp);

    if (*total_value > 100.0) *total_value = 100.0;
    if (*total_value < 0.0) *total_value = 0.0;
    if (*system_value > 100.0) *system_value = 100.0;
    if (*system_value < 0.0) *system_value = 0.0;

    if (*total_value < ctx->min_total) ctx->min_total = *total_value;
    if (*total_value > ctx->max_total) ctx->max_total = *total_value;
    if (*system_value < ctx->min_system) ctx->min_system = *system_value;
    if (*system_value > ctx->max_system) ctx->max_system = *system_value;
    ctx->sum_total += (uint64_t)*total_value;
    ctx->sum_system += (uint64_t)*system_value;
    ctx->last_total = *total_value;
    ctx->last_system = *system_value;
    ctx->sample_count++;

    return 1;
#else
    return 0;
#endif
}

static int cpu_get_stats(void *context, datasource_stats_t *stats) {
    cpu_stats_t *ctx = (cpu_stats_t *)context;
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

    stats->min = ctx->min_total;
    stats->max = ctx->max_total;
    stats->avg = (double)(ctx->sum_total / ctx->sample_count);
    stats->last = ctx->last_total;
    stats->min_secondary = ctx->min_system;
    stats->max_secondary = ctx->max_system;
    stats->avg_secondary = (double)(ctx->sum_system / ctx->sample_count);
    stats->last_secondary = ctx->last_system;

    return 1;
}

static void cpu_cleanup(void *context) {
    free(context);
}

static void cpu_format_value(double value, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%.1f%%", value);
}

datasource_handler_t cpu_handler = {
    .init = cpu_init,
    .collect = cpu_collect,
    .collect_dual = cpu_collect_dual,
    .get_stats = cpu_get_stats,
    .format_value = cpu_format_value,
    .cleanup = cpu_cleanup,
    .name = "cpu",
    .unit = "%",
    .is_dual = 1,
    .max_scale = 100.0
};
