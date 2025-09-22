#define _GNU_SOURCE
#include "../datasource.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>

#ifdef __APPLE__
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
    bool first_sample;
    double last_in_rate;   // IN rate in bytes/s
    double last_out_rate;  // OUT rate in bytes/s
    double last_rate;      // Combined rate for backward compatibility
} if_thr_context_t;

#ifdef __APPLE__
static bool get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
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
        return false;
    }

    buf = malloc(len);
    if (!buf) {
        return false;
    }

    if (sysctl(mib, 6, buf, &len, NULL, 0) < 0) {
        free(buf);
        return false;
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
                    return true;
                }
            }
        }

        next += ifm->ifm_msglen;
    }

    free(buf);
    return false;
}
#elif defined(LINUX)
static bool get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        return false;
    }

    char line[256];
    // Skip header lines
    fgets(line, sizeof(line), fp);
    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp)) {
        char ifname[32];
        unsigned long rx_bytes, tx_bytes;
        unsigned long rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame, rx_compressed, rx_multicast;
        unsigned long tx_packets, tx_errs, tx_drop, tx_fifo, tx_colls, tx_carrier, tx_compressed;

        // Parse the line: interface: rx_bytes rx_packets ... tx_bytes tx_packets ...
        if (sscanf(line, " %31[^:]: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                   ifname, &rx_bytes, &rx_packets, &rx_errs, &rx_drop, &rx_fifo, &rx_frame, &rx_compressed, &rx_multicast,
                   &tx_bytes, &tx_packets, &tx_errs, &tx_drop, &tx_fifo, &tx_colls, &tx_carrier, &tx_compressed) == 17) {

            if (strcmp(ifname, interface_name) == 0) {
                *in_bytes = (uint32_t)rx_bytes;
                *out_bytes = (uint32_t)tx_bytes;
                fclose(fp);
                return true;
            }
        }
    }

    fclose(fp);
    return false;
}
#endif

static void format_rate_human_readable(double bytes_per_sec, char* buffer, size_t buffer_size) {
    if (bytes_per_sec >= 1073741824.0) { // 1GB
        snprintf(buffer, buffer_size, "%.1f GB/s", bytes_per_sec / 1073741824.0);
    } else if (bytes_per_sec >= 1048576.0) { // 1MB
        snprintf(buffer, buffer_size, "%.1f MB/s", bytes_per_sec / 1048576.0);
    } else if (bytes_per_sec >= 1024.0) { // 1KB
        snprintf(buffer, buffer_size, "%.1f KB/s", bytes_per_sec / 1024.0);
    } else {
        snprintf(buffer, buffer_size, "%.1f B/s", bytes_per_sec);
    }
}

// Parse target format: local,interface_name
// Example: "local,en0"
static bool parse_if_thr_target(const char* target, char** interface_name) {
    if (!target) return false;

    char* target_copy = strdup(target);
    char* type_str = strtok(target_copy, ",");
    char* interface_str = strtok(NULL, ",");

    if (!type_str || !interface_str || strcmp(type_str, "local") != 0) {
        free(target_copy);
        return false;
    }

    *interface_name = strdup(interface_str);

    free(target_copy);
    return (*interface_name != NULL);
}

static bool if_thr_init(const char *target, void **context) {
    if (!target) return false;

#if !defined(__APPLE__) && !defined(LINUX)
    return false; // Only supported on macOS and Linux
#endif

    if_thr_context_t *ctx = malloc(sizeof(if_thr_context_t));
    if (!ctx) return false;

    if (!parse_if_thr_target(target, &ctx->interface_name)) {
        free(ctx);
        return false;
    }

    ctx->prev_in_bytes = 0;
    ctx->prev_out_bytes = 0;
    ctx->prev_time = 0;
    ctx->first_sample = true;
    ctx->last_in_rate = 0.0;
    ctx->last_out_rate = 0.0;
    ctx->last_rate = 0.0;

    *context = ctx;
    return true;
}

// Shared collection logic for all if_thr variants
static bool if_thr_collect_internal(if_thr_context_t *ctx) {
#if !defined(__APPLE__) && !defined(LINUX)
    return false; // Only supported on macOS and Linux
#endif

    uint32_t in_bytes, out_bytes;
    time_t current_time = time(NULL);

    if (!get_interface_stats(ctx->interface_name, &in_bytes, &out_bytes)) {
        return false;
    }

    if (ctx->first_sample) {
        ctx->prev_in_bytes = in_bytes;
        ctx->prev_out_bytes = out_bytes;
        ctx->prev_time = current_time;
        ctx->first_sample = false;
        ctx->last_in_rate = 0.0;
        ctx->last_out_rate = 0.0;
        ctx->last_rate = 0.0;
        return true;
    }

    time_t time_diff = current_time - ctx->prev_time;
    if (time_diff <= 0) {
        return true; // Return previous rates if time didn't advance
    }

    // Handle counter wraps using uint32_t arithmetic (like SNMP)
    uint32_t in_diff = (uint32_t)(in_bytes - ctx->prev_in_bytes);
    uint32_t out_diff = (uint32_t)(out_bytes - ctx->prev_out_bytes);

    // Calculate separate IN and OUT rates in bytes per second
    double in_rate_bps = (double)in_diff / (double)time_diff;
    double out_rate_bps = (double)out_diff / (double)time_diff;

    char in_str[64], out_str[64];
    format_rate_human_readable(in_rate_bps, in_str, sizeof(in_str));
    format_rate_human_readable(out_rate_bps, out_str, sizeof(out_str));
    printf("BW %s IN: %s, OUT: %s\n", ctx->interface_name, in_str, out_str);

    ctx->prev_in_bytes = in_bytes;
    ctx->prev_out_bytes = out_bytes;
    ctx->prev_time = current_time;
    ctx->last_in_rate = in_rate_bps;   // Store as B/s, plotting system will format dynamically
    ctx->last_out_rate = out_rate_bps; // Store as B/s, plotting system will format dynamically
    ctx->last_rate = in_rate_bps + out_rate_bps;

    return true;
}

static bool if_thr_collect(void *context, double *value) {
    if_thr_context_t *ctx = (if_thr_context_t *)context;
    if (!ctx || !value) return false;

    if (!if_thr_collect_internal(ctx)) {
        *value = -1.0;
        return false;
    }

    *value = ctx->last_rate; // Return combined rate
    return true;
}

static bool if_thr_collect_dual(void *context, double *in_value, double *out_value) {
    if_thr_context_t *ctx = (if_thr_context_t *)context;
    if (!ctx || !in_value || !out_value) return false;

    if (!if_thr_collect_internal(ctx)) {
        *in_value = -1.0;
        *out_value = -1.0;
        return false;
    }

    *in_value = ctx->last_in_rate;   // Return IN rate
    *out_value = ctx->last_out_rate; // Return OUT rate
    return true;
}

static void if_thr_cleanup(void *context) {
    if_thr_context_t *ctx = (if_thr_context_t *)context;
    if (!ctx) return;

    free(ctx->interface_name);
    free(ctx);
}

datasource_handler_t if_thr_handler = {
    .init = if_thr_init,
    .collect = if_thr_collect,
    .collect_dual = if_thr_collect_dual,
    .cleanup = if_thr_cleanup,
    .name = "if_thr",
    .unit = "B/s",
    .is_dual = true
};