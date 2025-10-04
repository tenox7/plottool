#ifndef PLATFORM_H
#define PLATFORM_H

#include "compat.h"

typedef struct {
    uint64_t total_memory;
    uint64_t free_memory;
    double cpu_usage;
    double system_cpu_usage;
    uint64_t disk_total;
    uint64_t disk_free;
    double load_avg;
} system_stats_t;

typedef struct {
    void *handle;
} mutex_t;

typedef struct {
    void *handle;
} plot_thread_t;


int platform_init(void);
void platform_cleanup(void);
void platform_sleep(uint32_t milliseconds);
uint32_t platform_get_time_ms(void);

mutex_t *mutex_create(void);
void mutex_destroy(mutex_t *mutex);
void mutex_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);

plot_thread_t *plot_thread_create(void (*func)(void *), void *arg);
void plot_thread_destroy(plot_thread_t *thread);
void plot_thread_join(plot_thread_t *thread);
int plot_thread_join_timeout(plot_thread_t *thread, uint32_t timeout_ms);

char *get_platform_config_path(const char *filename);

#endif
