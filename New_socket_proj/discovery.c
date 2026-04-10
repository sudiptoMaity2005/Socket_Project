#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "discovery.h"
#include "common.h"
#include "net_utils.h"
#include "peer_list.h"

/* ─── Shared state ───────────────────────────────────────────────────────── */
static uint16_t g_own_port  = DEFAULT_WORKER_PORT;
static char     g_local_ip[INET_ADDRSTRLEN] = "127.0.0.1";
static int      g_bcast_fd  = -1;   /* broadcast send socket */

/* ─── Build a HELLO/BYE payload ──────────────────────────────────────────── */
static payload_hello_t build_hello(void) {
    payload_hello_t h;
    memset(&h, 0, sizeof(h));
    h.tcp_port = htons(g_own_port);
    gethostname(h.hostname, sizeof(h.hostname) - 1);
    return h;
}

/* ─── Broadcast a single HELLO or BYE ───────────────────────────────────── */
static void send_broadcast(msg_type_t type) {
    if (g_bcast_fd < 0) return;

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_port        = htons(DISCOVERY_PORT);
    dst.sin_addr.s_addr = htonl(INADDR_BROADCAST); /* 255.255.255.255 */

    payload_hello_t h = build_hello();

    msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type        = (uint8_t)type;
    hdr.payload_len = htonl((uint32_t)sizeof(h));

    /* Single atomic sendto combining header+payload via iovec */
    uint8_t pkt[sizeof(hdr) + sizeof(h)];
    memcpy(pkt,              &hdr, sizeof(hdr));
    memcpy(pkt + sizeof(hdr), &h,  sizeof(h));

    sendto(g_bcast_fd, pkt, sizeof(pkt), 0,
           (struct sockaddr *)&dst, sizeof(dst));
}

/* ─── Announcer thread ───────────────────────────────────────────────────── */
static void *announcer_thread(void *arg) {
    (void)arg;
    for (;;) {
        send_broadcast(MSG_HELLO);
        sleep(DISCOVERY_INTERVAL);
    }
    return NULL;
}

/* ─── Listener thread ────────────────────────────────────────────────────── */
static void *listener_thread(void *arg) {
    (void)arg;

    int listen_fd = make_udp_broadcast_socket(DISCOVERY_PORT);
    if (listen_fd < 0) {
        LOG("Failed to create discovery listener socket");
        return NULL;
    }

    uint8_t buf[sizeof(msg_header_t) + sizeof(payload_hello_t) + 16];

    for (;;) {
        struct sockaddr_in src;
        socklen_t src_len = sizeof(src);
        ssize_t n = recvfrom(listen_fd, buf, sizeof(buf), 0,
                             (struct sockaddr *)&src, &src_len);
        if (n < (ssize_t)sizeof(msg_header_t)) continue;

        msg_header_t hdr;
        memcpy(&hdr, buf, sizeof(hdr));
        uint32_t plen = ntohl(hdr.payload_len);

        /* Validate total packet length */
        if (n < (ssize_t)(sizeof(msg_header_t) + plen)) continue;

        char src_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &src.sin_addr, src_ip, sizeof(src_ip));

        /* Ignore our own broadcasts */
        if (strcmp(src_ip, g_local_ip) == 0) continue;

        switch ((msg_type_t)hdr.type) {
            case MSG_HELLO: {
                if (plen < sizeof(payload_hello_t)) break;
                payload_hello_t h;
                memcpy(&h, buf + sizeof(hdr), sizeof(h));
                uint16_t port = ntohs(h.tcp_port);
                peer_list_add(src_ip, port);
                peer_list_touch(src_ip);
                break;
            }
            case MSG_BYE:
                peer_list_remove(src_ip);
                break;
            default:
                break;
        }
    }
    return NULL;
}

/* ─── Reaper thread ──────────────────────────────────────────────────────── */
static void *reaper_thread(void *arg) {
    (void)arg;
    for (;;) {
        sleep(PEER_TIMEOUT_SECS / 2);
        peer_list_reap_stale();
    }
    return NULL;
}

/* ─── discovery_start ────────────────────────────────────────────────────── */
void discovery_start(uint16_t own_tcp_port, const char *local_ip) {
    g_own_port = own_tcp_port;
    strncpy(g_local_ip, local_ip, INET_ADDRSTRLEN - 1);

    g_bcast_fd = make_udp_broadcast_socket(0); /* unbound send socket */
    if (g_bcast_fd < 0) DIE("Cannot create broadcast send socket");

    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    pthread_create(&t, &attr, announcer_thread, NULL);
    pthread_create(&t, &attr, listener_thread, NULL);
    pthread_create(&t, &attr, reaper_thread, NULL);

    pthread_attr_destroy(&attr);
    LOG("Discovery started (local %s, TCP port %u)", local_ip, own_tcp_port);
}

/* ─── discovery_send_bye ─────────────────────────────────────────────────── */
void discovery_send_bye(void) {
    send_broadcast(MSG_BYE);
    LOG("BYE broadcast sent");
}
