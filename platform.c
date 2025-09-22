#define _GNU_SOURCE
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

#if defined(__unix__) || defined(__unix) || defined(unix) || defined(__APPLE__)
#include <pthread.h>
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#elif defined(__unix__) || defined(__unix) || defined(unix)
#include <time.h>
#endif

static bool platform_initialized = false;

bool platform_init(void) {
    if (platform_initialized) {
        return true;
    }

    platform_initialized = true;
    return true;
}

void platform_cleanup(void) {
    platform_initialized = false;
}

void platform_sleep(uint32_t milliseconds) {
    usleep(milliseconds * 1000);
}

mutex_t *mutex_create(void) {
#if defined(__unix__) || defined(__unix) || defined(unix) || defined(__APPLE__)
    mutex_t *mutex = malloc(sizeof(mutex_t));
    if (!mutex) return NULL;

    mutex->handle = malloc(sizeof(pthread_mutex_t));
    if (!mutex->handle) {
        free(mutex);
        return NULL;
    }

    if (pthread_mutex_init((pthread_mutex_t*)mutex->handle, NULL) != 0) {
        free(mutex->handle);
        free(mutex);
        return NULL;
    }

    return mutex;
#else
    return NULL;
#endif
}

void mutex_destroy(mutex_t *mutex) {
    if (!mutex) return;

#if defined(__unix__) || defined(__unix) || defined(unix) || defined(__APPLE__)
    pthread_mutex_destroy((pthread_mutex_t*)mutex->handle);
    free(mutex->handle);
#endif
    free(mutex);
}

void mutex_lock(mutex_t *mutex) {
    if (!mutex) return;

#if defined(__unix__) || defined(__unix) || defined(unix) || defined(__APPLE__)
    pthread_mutex_lock((pthread_mutex_t*)mutex->handle);
#endif
}

void mutex_unlock(mutex_t *mutex) {
    if (!mutex) return;

#if defined(__unix__) || defined(__unix) || defined(unix) || defined(__APPLE__)
    pthread_mutex_unlock((pthread_mutex_t*)mutex->handle);
#endif
}

plot_thread_t *plot_thread_create(void (*func)(void *), void *arg) {
#if defined(__unix__) || defined(__unix) || defined(unix) || defined(__APPLE__)
    plot_thread_t *thread = malloc(sizeof(plot_thread_t));
    if (!thread) return NULL;

    thread->handle = malloc(sizeof(pthread_t));
    if (!thread->handle) {
        free(thread);
        return NULL;
    }

    if (pthread_create((pthread_t*)thread->handle, NULL, (void*(*)(void*))func, arg) != 0) {
        free(thread->handle);
        free(thread);
        return NULL;
    }

    return thread;
#else
    return NULL;
#endif
}

void plot_thread_destroy(plot_thread_t *thread) {
    if (!thread) return;

#if defined(__unix__) || defined(__unix) || defined(unix) || defined(__APPLE__)
    free(thread->handle);
#endif
    free(thread);
}

void plot_thread_join(plot_thread_t *thread) {
    if (!thread) return;

#if defined(__unix__) || defined(__unix) || defined(unix) || defined(__APPLE__)
    pthread_join(*(pthread_t*)thread->handle, NULL);
#endif
}

bool get_system_stats(system_stats_t *stats) {
    if (!stats) return false;

#ifdef __APPLE__
    int mib[2];
    size_t len;

    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    len = sizeof(stats->total_memory);
    if (sysctl(mib, 2, &stats->total_memory, &len, NULL, 0) != 0) {
        return false;
    }

    vm_statistics64_data_t vm_stats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                         (host_info64_t)&vm_stats, &count) != KERN_SUCCESS) {
        return false;
    }

    stats->free_memory = vm_stats.free_count * 4096;

    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;
    static uint64_t prev_system = 0;

    processor_info_array_t cpu_info;
    mach_msg_type_number_t num_cpu_info;
    natural_t num_processors;

    if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
                           &num_processors, &cpu_info, &num_cpu_info) != KERN_SUCCESS) {
        return false;
    }

    uint64_t idle = 0, total = 0, system = 0;
    for (natural_t i = 0; i < num_processors; i++) {
        processor_cpu_load_info_t cpu_load = (processor_cpu_load_info_t)cpu_info;
        idle += cpu_load[i].cpu_ticks[CPU_STATE_IDLE];
        system += cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM];
        total += cpu_load[i].cpu_ticks[CPU_STATE_IDLE] +
                cpu_load[i].cpu_ticks[CPU_STATE_USER] +
                cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM] +
                cpu_load[i].cpu_ticks[CPU_STATE_NICE];
    }

    if (prev_total != 0) {
        uint64_t idle_diff = idle - prev_idle;
        uint64_t total_diff = total - prev_total;
        uint64_t system_diff = system - prev_system;

        stats->cpu_usage = 100.0 * (1.0 - (double)idle_diff / total_diff);
        stats->system_cpu_usage = 100.0 * (double)system_diff / total_diff;
    } else {
        stats->cpu_usage = 0.0;
        stats->system_cpu_usage = 0.0;
    }

    prev_idle = idle;
    prev_total = total;
    prev_system = system;

    vm_deallocate(mach_task_self(), (vm_address_t)cpu_info,
                  num_cpu_info * sizeof(integer_t));

    return true;
#elif defined(__unix__) || defined(__unix) || defined(unix)
    static uint64_t prev_idle = 0;
    static uint64_t prev_total = 0;
    static uint64_t prev_system = 0;

    // Get memory info from /proc/meminfo
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return false;

    char line[256];
    uint64_t mem_total = 0, mem_available = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "MemTotal: %lu kB", &mem_total) == 1) {
            stats->total_memory = mem_total * 1024;
        } else if (sscanf(line, "MemAvailable: %lu kB", &mem_available) == 1) {
            stats->free_memory = mem_available * 1024;
        }
    }
    fclose(fp);

    // Get CPU info from /proc/stat
    fp = fopen("/proc/stat", "r");
    if (!fp) return false;

    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
    if (fgets(line, sizeof(line), fp) &&
        sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 8) {

        uint64_t total_ticks = user + nice + system + idle + iowait + irq + softirq + steal;
        uint64_t system_ticks = system + irq + softirq;

        if (prev_total != 0) {
            uint64_t idle_diff = idle - prev_idle;
            uint64_t total_diff = total_ticks - prev_total;
            uint64_t system_diff = system_ticks - prev_system;

            if (total_diff > 0) {
                stats->cpu_usage = 100.0 * (1.0 - (double)idle_diff / total_diff);
                stats->system_cpu_usage = 100.0 * (double)system_diff / total_diff;
            } else {
                stats->cpu_usage = 0.0;
                stats->system_cpu_usage = 0.0;
            }
        } else {
            stats->cpu_usage = 0.0;
            stats->system_cpu_usage = 0.0;
        }

        prev_idle = idle;
        prev_total = total_ticks;
        prev_system = system_ticks;
    }
    fclose(fp);

    return true;
#else
    return false;
#endif
}

