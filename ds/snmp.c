#define _GNU_SOURCE
#include "../datasource.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/select.h>

#define SNMP_PORT 161
#define SNMP_VERSION_1 0
#define SNMP_GET_REQUEST 0xA0
#define SNMP_GET_RESPONSE 0xA2

#define ASN1_INTEGER 0x02
#define ASN1_OCTET_STRING 0x04
#define ASN1_NULL 0x05
#define ASN1_OBJECT_ID 0x06
#define ASN1_SEQUENCE 0x30
#define ASN1_COUNTER32 0x41

#define SNMP_ERR_SUCCESS 0
#define SNMP_ERR_SOCKET -1
#define SNMP_ERR_RESOLVE -2
#define SNMP_ERR_SEND -3
#define SNMP_ERR_RECV -4
#define SNMP_ERR_PARSE -5
#define SNMP_ERR_TIMEOUT -6

typedef struct {
    char *hostname;
    char *community;
    int interface_index;
    uint32_t prev_in_octets;
    uint32_t prev_out_octets;
    time_t prev_time;
    bool first_sample;
    double last_in_rate;   // IN rate in bytes/s
    double last_out_rate;  // OUT rate in bytes/s
    double last_rate;      // Combined rate for backward compatibility
} snmp_context_t;

static uint8_t packet[1500];
static int packet_len = 0;

static void encode_length(int len) {
    if (len < 128) {
        packet[packet_len++] = len;
    } else if (len < 256) {
        packet[packet_len++] = 0x81;
        packet[packet_len++] = len;
    } else {
        packet[packet_len++] = 0x82;
        packet[packet_len++] = (len >> 8) & 0xFF;
        packet[packet_len++] = len & 0xFF;
    }
}

static void encode_integer(int value) {
    packet[packet_len++] = ASN1_INTEGER;
    packet[packet_len++] = 1;
    packet[packet_len++] = value;
}

static void encode_string(const char* str) {
    int len = strlen(str);
    packet[packet_len++] = ASN1_OCTET_STRING;
    encode_length(len);
    memcpy(&packet[packet_len], str, len);
    packet_len += len;
}

static void encode_oid(const char* oid_str) {
    uint32_t oids[128];
    int oid_count = 0;
    char* oid_copy = strdup(oid_str);
    char* token = strtok(oid_copy, ".");

    while (token && oid_count < 128) {
        oids[oid_count++] = atoi(token);
        token = strtok(NULL, ".");
    }
    free(oid_copy);

    if (oid_count < 2) return;

    packet[packet_len++] = ASN1_OBJECT_ID;

    int start_len = packet_len++;
    packet[packet_len++] = oids[0] * 40 + oids[1];

    for (int i = 2; i < oid_count; i++) {
        uint32_t val = oids[i];
        if (val < 128) {
            packet[packet_len++] = val;
        } else {
            uint8_t bytes[5];
            int byte_count = 0;
            while (val > 0) {
                bytes[byte_count] = (val & 0x7F) | (byte_count > 0 ? 0x80 : 0);
                byte_count++;
                val >>= 7;
            }
            for (int j = byte_count - 1; j >= 0; j--) {
                packet[packet_len++] = bytes[j];
            }
        }
    }

    packet[start_len] = packet_len - start_len - 1;
}

static void encode_null(void) {
    packet[packet_len++] = ASN1_NULL;
    packet[packet_len++] = 0;
}

static int parse_length(uint8_t* data, int* offset) {
    uint8_t first = data[(*offset)++];
    if (first & 0x80) {
        int len_bytes = first & 0x7F;
        int length = 0;
        for (int i = 0; i < len_bytes; i++) {
            length = (length << 8) | data[(*offset)++];
        }
        return length;
    }
    return first;
}

static uint32_t parse_integer(uint8_t* data, int* offset) {
    if (data[(*offset)++] != ASN1_INTEGER) return 0;
    int len = parse_length(data, offset);
    uint32_t value = 0;
    for (int i = 0; i < len; i++) {
        value = (value << 8) | data[(*offset)++];
    }
    return value;
}

static int snmp_get_uint32(const char* hostname, const char* community, const char* oid, uint32_t* result) {
    struct sockaddr_in server_addr;
    struct hostent* host_entry;
    int sockfd;

    *result = 0;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        return SNMP_ERR_SOCKET;
    }

    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SNMP_PORT);

    if (inet_aton(hostname, &server_addr.sin_addr) == 0) {
        host_entry = gethostbyname(hostname);
        if (host_entry == NULL) {
            close(sockfd);
            return SNMP_ERR_RESOLVE;
        }
        memcpy(&server_addr.sin_addr, host_entry->h_addr_list[0], host_entry->h_length);
    }

    packet_len = 0;

    packet[packet_len++] = ASN1_SEQUENCE;
    int msg_len_pos = packet_len++;

    encode_integer(SNMP_VERSION_1);
    encode_string(community);

    packet[packet_len++] = SNMP_GET_REQUEST;
    int pdu_len_pos = packet_len++;
    encode_integer(1234);
    encode_integer(0);
    encode_integer(0);

    packet[packet_len++] = ASN1_SEQUENCE;
    int varbind_list_len_pos = packet_len++;

    packet[packet_len++] = ASN1_SEQUENCE;
    int varbind_len_pos = packet_len++;

    encode_oid(oid);
    encode_null();

    packet[varbind_len_pos] = packet_len - varbind_len_pos - 1;
    packet[varbind_list_len_pos] = packet_len - varbind_list_len_pos - 1;
    packet[pdu_len_pos] = packet_len - pdu_len_pos - 1;
    packet[msg_len_pos] = packet_len - msg_len_pos - 1;

    if (sendto(sockfd, packet, packet_len, 0, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return SNMP_ERR_SEND;
    }

    uint8_t response[1500];
    socklen_t addr_len = sizeof(server_addr);
    int recv_len = recvfrom(sockfd, response, sizeof(response), 0, (struct sockaddr*)&server_addr, &addr_len);
    close(sockfd);

    if (recv_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return SNMP_ERR_TIMEOUT;
        }
        return SNMP_ERR_RECV;
    }

    int offset = 0;
    if (response[offset++] != ASN1_SEQUENCE) return SNMP_ERR_PARSE;
    parse_length(response, &offset);

    uint32_t version = parse_integer(response, &offset);
    if (version != SNMP_VERSION_1) return SNMP_ERR_PARSE;

    if (response[offset++] != ASN1_OCTET_STRING) return SNMP_ERR_PARSE;
    int comm_len = parse_length(response, &offset);
    offset += comm_len;

    if (response[offset++] != SNMP_GET_RESPONSE) return SNMP_ERR_PARSE;
    parse_length(response, &offset);

    parse_integer(response, &offset);

    uint32_t error_status = parse_integer(response, &offset);
    if (error_status != 0) return SNMP_ERR_PARSE;

    parse_integer(response, &offset);

    if (response[offset++] != ASN1_SEQUENCE) return SNMP_ERR_PARSE;
    parse_length(response, &offset);

    if (response[offset++] != ASN1_SEQUENCE) return SNMP_ERR_PARSE;
    parse_length(response, &offset);

    if (response[offset++] != ASN1_OBJECT_ID) return SNMP_ERR_PARSE;
    int oid_len = parse_length(response, &offset);
    offset += oid_len;

    uint8_t value_type = response[offset++];
    if (value_type != ASN1_INTEGER && value_type != ASN1_COUNTER32) return SNMP_ERR_PARSE;
    int val_len = parse_length(response, &offset);
    *result = 0;
    for (int i = 0; i < val_len; i++) {
        *result = (*result << 8) | response[offset++];
    }

    return SNMP_ERR_SUCCESS;
}

static int get_interface_throughput(const char* hostname, const char* community, int interface_index, uint32_t* in_octets, uint32_t* out_octets) {
    char oid[256];
    int result;

    // Get input octets (ifInOctets)
    snprintf(oid, sizeof(oid), "1.3.6.1.2.1.2.2.1.10.%d", interface_index);
    result = snmp_get_uint32(hostname, community, oid, in_octets);
    if (result != SNMP_ERR_SUCCESS) return result;

    // Get output octets (ifOutOctets)
    snprintf(oid, sizeof(oid), "1.3.6.1.2.1.2.2.1.16.%d", interface_index);
    result = snmp_get_uint32(hostname, community, oid, out_octets);

    return result;
}

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

// Parse target format: hostname,community,interface_index
// Example: "192.168.1.1,public,1"
static bool parse_snmp_target(const char* target, char** hostname, char** community, int* interface_index) {
    if (!target) return false;

    char* target_copy = strdup(target);
    char* hostname_str = strtok(target_copy, ",");
    char* community_str = strtok(NULL, ",");
    char* interface_str = strtok(NULL, ",");

    if (!hostname_str || !community_str || !interface_str) {
        free(target_copy);
        return false;
    }

    *hostname = strdup(hostname_str);
    *community = strdup(community_str);
    *interface_index = atoi(interface_str);

    free(target_copy);
    return (*hostname && *community);
}

static bool snmp_init(const char *target, void **context) {
    if (!target) return false;

    snmp_context_t *ctx = malloc(sizeof(snmp_context_t));
    if (!ctx) return false;

    if (!parse_snmp_target(target, &ctx->hostname, &ctx->community, &ctx->interface_index)) {
        free(ctx);
        return false;
    }

    ctx->prev_in_octets = 0;
    ctx->prev_out_octets = 0;
    ctx->prev_time = 0;
    ctx->first_sample = true;
    ctx->last_in_rate = 0.0;
    ctx->last_out_rate = 0.0;
    ctx->last_rate = 0.0;

    *context = ctx;
    return true;
}

// Shared collection logic for all SNMP variants
static bool snmp_collect_internal(snmp_context_t *ctx) {
    uint32_t in_octets, out_octets;
    time_t current_time = time(NULL);

    int result = get_interface_throughput(ctx->hostname, ctx->community, ctx->interface_index, &in_octets, &out_octets);
    if (result != SNMP_ERR_SUCCESS) {
        return false;
    }

    if (ctx->first_sample) {
        ctx->prev_in_octets = in_octets;
        ctx->prev_out_octets = out_octets;
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

    // Handle counter wraps using uint32_t arithmetic (like ttg.c)
    uint32_t in_diff = (uint32_t)(in_octets - ctx->prev_in_octets);
    uint32_t out_diff = (uint32_t)(out_octets - ctx->prev_out_octets);

    // Calculate separate IN and OUT rates in bytes per second
    double in_rate_bps = (double)in_diff / (double)time_diff;
    double out_rate_bps = (double)out_diff / (double)time_diff;

    char in_str[64], out_str[64];
    format_rate_human_readable(in_rate_bps, in_str, sizeof(in_str));
    format_rate_human_readable(out_rate_bps, out_str, sizeof(out_str));
    printf("SNMP %s:%d IN: %s, OUT: %s\n", ctx->hostname, ctx->interface_index, in_str, out_str);

    ctx->prev_in_octets = in_octets;
    ctx->prev_out_octets = out_octets;
    ctx->prev_time = current_time;
    ctx->last_in_rate = in_rate_bps;
    ctx->last_out_rate = out_rate_bps;
    ctx->last_rate = in_rate_bps + out_rate_bps;

    return true;
}

static bool snmp_collect(void *context, double *value) {
    snmp_context_t *ctx = (snmp_context_t *)context;
    if (!ctx || !value) return false;

    if (!snmp_collect_internal(ctx)) {
        *value = -1.0;
        return false;
    }

    *value = ctx->last_rate; // Return combined rate
    return true;
}


static bool snmp_collect_dual(void *context, double *in_value, double *out_value) {
    snmp_context_t *ctx = (snmp_context_t *)context;
    if (!ctx || !in_value || !out_value) return false;

    if (!snmp_collect_internal(ctx)) {
        *in_value = -1.0;
        *out_value = -1.0;
        return false;
    }

    *in_value = ctx->last_in_rate;   // Return IN rate
    *out_value = ctx->last_out_rate; // Return OUT rate
    return true;
}

static void snmp_cleanup(void *context) {
    snmp_context_t *ctx = (snmp_context_t *)context;
    if (!ctx) return;

    free(ctx->hostname);
    free(ctx->community);
    free(ctx);
}

datasource_handler_t snmp_handler = {
    .init = snmp_init,
    .collect = snmp_collect,
    .collect_dual = snmp_collect_dual,
    .cleanup = snmp_cleanup,
    .name = "snmp",
    .unit = "B/s",
    .is_dual = true
};

