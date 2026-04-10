#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>

#ifndef MSG_NOSIGNAL
#  define MSG_NOSIGNAL 0
#endif

#include "net_utils.h"
#include "common.h"

/* ─── send_all ───────────────────────────────────────────────────────────── */
int send_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

/* ─── recv_all ───────────────────────────────────────────────────────────── */
int recv_all(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        got += (size_t)n;
    }
    return 0;
}

/* ─── send_msg ───────────────────────────────────────────────────────────── */
int send_msg(int fd, msg_type_t type, const void *payload, uint32_t payload_len) {
    msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type        = (uint8_t)type;
    hdr.payload_len = htonl(payload_len);

    if (send_all(fd, &hdr, sizeof(hdr)) != 0) return -1;
    if (payload_len > 0 && payload != NULL)
        if (send_all(fd, payload, payload_len) != 0) return -1;
    return 0;
}

/* ─── recv_msg ───────────────────────────────────────────────────────────── */
int recv_msg(int fd, msg_type_t *type_out, uint8_t **payload_out, uint32_t *len_out) {
    msg_header_t hdr;
    if (recv_all(fd, &hdr, sizeof(hdr)) != 0) return -1;

    uint32_t plen = ntohl(hdr.payload_len);
    *type_out  = (msg_type_t)hdr.type;
    *len_out   = plen;
    *payload_out = NULL;

    if (plen > 0) {
        if (plen > MAX_FILE_SIZE + MAX_FILENAME_LEN + 8) {
            /* Sanity guard against runaway payloads */
            LOG("recv_msg: absurd payload_len=%u, dropping", plen);
            return -1;
        }
        *payload_out = (uint8_t *)malloc(plen + 1);
        if (!*payload_out) return -1;
        (*payload_out)[plen] = '\0'; /* null-terminate for string safety */
        if (recv_all(fd, *payload_out, plen) != 0) {
            free(*payload_out);
            *payload_out = NULL;
            return -1;
        }
    }
    return 0;
}

/* ─── get_local_ip ───────────────────────────────────────────────────────── */
int get_local_ip(const char *iface, char out_ip[INET_ADDRSTRLEN]) {
    struct ifaddrs *ifap, *ifa;
    if (getifaddrs(&ifap) != 0) return -1;

    /* First pass: try to match requested interface */
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        if (iface && strcmp(ifa->ifa_name, iface) != 0) continue;

        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &sa->sin_addr, out_ip, INET_ADDRSTRLEN);
        freeifaddrs(ifap);
        return 0;
    }

    /* Second pass: accept any non-loopback if specific iface not found */
    if (iface) {
        for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (ifa->ifa_flags & IFF_LOOPBACK) continue;

            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &sa->sin_addr, out_ip, INET_ADDRSTRLEN);
            LOG("iface '%s' not found, using '%s' (%s)", iface, ifa->ifa_name, out_ip);
            freeifaddrs(ifap);
            return 0;
        }
    }

    freeifaddrs(ifap);
    strncpy(out_ip, "127.0.0.1", INET_ADDRSTRLEN);
    return 0; /* fallback to loopback */
}

/* ─── make_udp_broadcast_socket ─────────────────────────────────────────── */
int make_udp_broadcast_socket(int port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket(UDP)"); return -1; }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    if (port > 0) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons((uint16_t)port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("bind(UDP)");
            close(fd);
            return -1;
        }
    }
    return fd;
}

/* ─── make_tcp_server ────────────────────────────────────────────────────── */
int make_tcp_server(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket(TCP)"); return -1; }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind(TCP)");
        close(fd);
        return -1;
    }
    if (listen(fd, 32) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }
    return fd;
}

/* ─── tcp_connect ────────────────────────────────────────────────────────── */
int tcp_connect(const char *ip, uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket(TCP connect)"); return -1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        LOG("Invalid IP: %s", ip);
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }
    return fd;
}
