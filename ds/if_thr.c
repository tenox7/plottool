#define _GNU_SOURCE
#include "../datasource.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <sys/sysctl.h>
#endif

typedef struct {
    char *interface_name;
    uint32_t prev_in_bytes;
    uint32_t prev_out_bytes;
    time_t prev_time;
    int first_sample;
    uint32_t last_in_rate;
    uint32_t last_out_rate;
    uint32_t last_rate;

    // Statistics tracking in native uint32_t
    uint32_t min_in_rate;
    uint32_t max_in_rate;
    uint32_t min_out_rate;
    uint32_t max_out_rate;
    uint32_t min_combined_rate;
    uint32_t max_combined_rate;
    uint64_t sum_in_rate;
    uint64_t sum_out_rate;
    uint64_t sum_combined_rate;
    uint32_t sample_count;
} if_thr_context_t;

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
static int get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
    int mib[6];
    size_t len;
    char *buf, *next, *lim;
    struct if_msghdr *ifm;
    struct sockaddr_dl *sdl;

    mib[0] = CTL_NET;
    mib[1] = PF_ROUTE;
    mib[2] = 0;
    mib[3] = 0;
    mib[4] = NET_RT_IFLIST;
    mib[5] = 0;

    if (sysctl(mib, 6, NULL, &len, NULL, 0) < 0) {
        return 0;
    }

    buf = malloc(len);
    if (!buf) {
        return 0;
    }

    if (sysctl(mib, 6, buf, &len, NULL, 0) < 0) {
        free(buf);
        return 0;
    }

    lim = buf + len;
    for (next = buf; next < lim;) {
        ifm = (struct if_msghdr *)next;

        if (ifm->ifm_type == RTM_IFINFO) {
            sdl = (struct sockaddr_dl *)(ifm + 1);
            if (sdl->sdl_family == AF_LINK) {
                char ifname[32];
                memcpy(ifname, sdl->sdl_data, sdl->sdl_nlen);
                ifname[sdl->sdl_nlen] = '\0';

                if (strcmp(ifname, interface_name) == 0) {
                    *in_bytes = ifm->ifm_data.ifi_ibytes;
                    *out_bytes = ifm->ifm_data.ifi_obytes;
                    free(buf);
                    return 1;
                }
            }
        }

        next += ifm->ifm_msglen;
    }

    free(buf);
    return 0;
}
#elif defined(LINUX)
static int get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        return 0;
    }

    char line[256];
    fgets(line, sizeof(line), fp);
    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp)) {
        char ifname[32];
        unsigned long rx_bytes, tx_bytes;
        unsigned long rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame, rx_compressed, rx_multicast;
        unsigned long tx_packets, tx_errs, tx_drop, tx_fifo, tx_colls, tx_carrier, tx_compressed;

        if (sscanf(line, " %31[^:]: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                   ifname, &rx_bytes, &rx_packets, &rx_errs, &rx_drop, &rx_fifo, &rx_frame, &rx_compressed, &rx_multicast,
                   &tx_bytes, &tx_packets, &tx_errs, &tx_drop, &tx_fifo, &tx_colls, &tx_carrier, &tx_compressed) == 17) {

            if (strcmp(ifname, interface_name) == 0) {
                *in_bytes = (uint32_t)rx_bytes;
                *out_bytes = (uint32_t)tx_bytes;
                fclose(fp);
                return 1;
            }
        }
    }

    fclose(fp);
    return 0;
}
#elif defined(__sun) || defined(__SVR4) || defined(__svr4__)
static int get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
    return 0;
}
#endif

static void format_rate_human_readable(double disp_bytes_per_sec, char* buffer, size_t buffer_size) {
    if (disp_bytes_per_sec >= 1073741824.0) {
        snprintf(buffer, buffer_size, "%.1f GB/s", disp_bytes_per_sec / 1073741824.0);
    } else if (disp_bytes_per_sec >= 1048576.0) {
        snprintf(buffer, buffer_size, "%.1f MB/s", disp_bytes_per_sec / 1048576.0);
    } else if (disp_bytes_per_sec >= 1024.0) {
        snprintf(buffer, buffer_size, "%.1f KB/s", disp_bytes_per_sec / 1024.0);
    } else {
        snprintf(buffer, buffer_size, "%.1f B/s", disp_bytes_per_sec);
    }
}

static int parse_if_thr_target(const char* target, char** interface_name) {
    if (!target) return 0;

    char* target_copy = strdup(target);
    char* type_str = strtok(target_copy, ",");
    char* interface_str = strtok(NULL, ",");

    if (!type_str || !interface_str || strcmp(type_str, "local") != 0) {
        free(target_copy);
        return 0;
    }

    *interface_name = strdup(interface_str);

    free(target_copy);
    return (*interface_name != NULL);
}

static int if_thr_init(const char *target, void **context) {
    if (!target) return 0;

#if !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__NetBSD__) && !defined(__OpenBSD__) && !defined(LINUX)
    return 0;
#endif

    if_thr_context_t *ctx = malloc(sizeof(if_thr_context_t));
    if (!ctx) return 0;

    if (!parse_if_thr_target(target, &ctx->interface_name)) {
        free(ctx);
        return 0;
    }

    ctx->prev_in_bytes = 0;
    ctx->prev_out_bytes = 0;
    ctx->prev_time = 0;
    ctx->first_sample = 1;
    ctx->last_in_rate = 0;
    ctx->last_out_rate = 0;
    ctx->last_rate = 0;

    ctx->min_in_rate = UINT32_MAX;
    ctx->max_in_rate = 0;
    ctx->min_out_rate = UINT32_MAX;
    ctx->max_out_rate = 0;
    ctx->min_combined_rate = UINT32_MAX;
    ctx->max_combined_rate = 0;
    ctx->sum_in_rate = 0;
    ctx->sum_out_rate = 0;
    ctx->sum_combined_rate = 0;
    ctx->sample_count = 0;

    *context = ctx;
    return 1;
}

static int if_thr_collect_internal(if_thr_context_t *ctx) {
#if !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__NetBSD__) && !defined(__OpenBSD__) && !defined(LINUX)
    return 0;
#endif

    uint32_t in_bytes, out_bytes;
    time_t current_time = time(NULL);

    if (!get_interface_stats(ctx->interface_name, &in_bytes, &out_bytes)) {
        return 0;
    }

    if (ctx->first_sample) {
        ctx->prev_in_bytes = in_bytes;
        ctx->prev_out_bytes = out_bytes;
        ctx->prev_time = current_time;
        ctx->first_sample = 0;
        ctx->last_in_rate = 0;
        ctx->last_out_rate = 0;
        ctx->last_rate = 0;
        return 1;
    }

    time_t time_diff = current_time - ctx->prev_time;
    if (time_diff <= 0) {
        return 1;
    }

    uint32_t in_diff = in_bytes - ctx->prev_in_bytes;
    uint32_t out_diff = out_bytes - ctx->prev_out_bytes;

    uint32_t in_rate_bps = in_diff / time_diff;
    uint32_t out_rate_bps = out_diff / time_diff;
    uint32_t combined_rate_bps = in_rate_bps + out_rate_bps;

    // Update statistics with uint32_t values
    if (in_rate_bps < ctx->min_in_rate) ctx->min_in_rate = in_rate_bps;
    if (in_rate_bps > ctx->max_in_rate) ctx->max_in_rate = in_rate_bps;
    if (out_rate_bps < ctx->min_out_rate) ctx->min_out_rate = out_rate_bps;
    if (out_rate_bps > ctx->max_out_rate) ctx->max_out_rate = out_rate_bps;
    if (combined_rate_bps < ctx->min_combined_rate) ctx->min_combined_rate = combined_rate_bps;
    if (combined_rate_bps > ctx->max_combined_rate) ctx->max_combined_rate = combined_rate_bps;

    ctx->sum_in_rate += in_rate_bps;
    ctx->sum_out_rate += out_rate_bps;
    ctx->sum_combined_rate += combined_rate_bps;
    ctx->sample_count++;

    ctx->prev_in_bytes = in_bytes;
    ctx->prev_out_bytes = out_bytes;
    ctx->prev_time = current_time;
    ctx->last_in_rate = in_rate_bps;
    ctx->last_out_rate = out_rate_bps;
    ctx->last_rate = combined_rate_bps;

    return 1;
}

static int if_thr_collect(void *context, double *disp_value) {
    if_thr_context_t *ctx = (if_thr_context_t *)context;
    if (!ctx || !disp_value) return 0;

    if (!if_thr_collect_internal(ctx)) {
        *disp_value = -1.0;
        return 0;
    }

    *disp_value = (double)ctx->last_rate;
    return 1;
}

static int if_thr_collect_dual(void *context, double *disp_in_value, double *disp_out_value) {
    if_thr_context_t *ctx = (if_thr_context_t *)context;
    if (!ctx || !disp_in_value || !disp_out_value) return 0;

    if (!if_thr_collect_internal(ctx)) {
        *disp_in_value = -1.0;
        *disp_out_value = -1.0;
        return 0;
    }

    *disp_in_value = (double)ctx->last_in_rate;
    *disp_out_value = (double)ctx->last_out_rate;
    return 1;
}

static int if_thr_get_stats(void *context, datasource_stats_t *stats) {
    if_thr_context_t *ctx = (if_thr_context_t *)context;
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

    // Convert from uint32_t to double for display
    stats->min = (double)ctx->min_in_rate;
    stats->max = (double)ctx->max_in_rate;
    stats->avg = (double)(ctx->sum_in_rate / ctx->sample_count);
    stats->last = (double)ctx->last_in_rate;
    stats->min_secondary = (double)ctx->min_out_rate;
    stats->max_secondary = (double)ctx->max_out_rate;
    stats->avg_secondary = (double)(ctx->sum_out_rate / ctx->sample_count);
    stats->last_secondary = (double)ctx->last_out_rate;

    return 1;
}

static void if_thr_cleanup(void *context) {
    if_thr_context_t *ctx = (if_thr_context_t *)context;
    if (!ctx) return;

    free(ctx->interface_name);
    free(ctx);
}

static void if_thr_format_value(double value, char *buffer, size_t buffer_size) {
    format_rate_human_readable(value, buffer, buffer_size);
}

datasource_handler_t if_thr_handler = {
    .init = if_thr_init,
    .collect = if_thr_collect,
    .collect_dual = if_thr_collect_dual,
    .get_stats = if_thr_get_stats,
    .format_value = if_thr_format_value,
    .cleanup = if_thr_cleanup,
    .name = "if_thr",
    .unit = "B/s",
    .is_dual = 1,
    .max_scale = 0.0
};
