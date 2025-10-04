#define _GNU_SOURCE
#include "compat.h"
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
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/sysctl.h>
#include <sys/types.h>
#include <time.h>
#if defined(__NetBSD__) || defined(__OpenBSD__)
#include <uvm/uvm_extern.h>
#endif
#elif defined(__sun__) || defined(__sun) || defined(sun)
#include <kstat.h>
#include <sys/sysinfo.h>
#include <time.h>
#elif defined(__unix__) || defined(__unix) || defined(unix)
#include <time.h>
#endif

static int platform_initialized = 0;

int platform_init(void) {
    if (platform_initialized) {
        return 1;
    }

    platform_initialized = 1;
    return 1;
}

void platform_cleanup(void) {
    platform_initialized = 0;
}

void platform_sleep(uint32_t milliseconds) {
#ifdef __hpux
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
#else
    usleep(milliseconds * 1000);
#endif
}

uint32_t platform_get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
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

int plot_thread_join_timeout(plot_thread_t *thread, uint32_t timeout_ms) {
    if (!thread) return 0;

#if defined(__unix__) || defined(__unix) || defined(unix) || defined(__APPLE__)
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }

    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }

#ifdef __APPLE__
    int result = pthread_join(*(pthread_t*)thread->handle, NULL);
    return (result == 0);
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__sun__) || defined(__hpux)
    int result = pthread_join(*(pthread_t*)thread->handle, NULL);
    return (result == 0);
#else
    int result = pthread_timedjoin_np(*(pthread_t*)thread->handle, NULL, &ts);
    return (result == 0);
#endif
#else
    return 0;
#endif
}


char *get_platform_config_path(const char *filename) {
    char *home;
    static char config_path[512];
    char *xdg_config;

    home = getenv("HOME");
    if (!home) return NULL;

#ifdef __APPLE__
    snprintf(config_path, sizeof(config_path), "%s/Library/Preferences/%s", home, filename);
#elif defined(__unix__) || defined(__unix) || defined(unix)
    xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config) {
        snprintf(config_path, sizeof(config_path), "%s/plottool/%s", xdg_config, filename);
    } else {
        snprintf(config_path, sizeof(config_path), "%s/.config/plottool/%s", home, filename);
    }
#else
    return NULL;
#endif

    return config_path;
}

