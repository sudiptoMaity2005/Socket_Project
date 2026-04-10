#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "dispatcher.h"
#include "common.h"
#include "net_utils.h"
#include "peer_list.h"
#include "load_monitor.h"

/* ─── Query a single peer for its load; returns load score or FLT_MAX ────── */
static float query_peer_load(const char *ip, uint16_t port,
                              peer_info_t *out_info) {
    int udp_fd = make_udp_broadcast_socket(0);
    if (udp_fd < 0) return 1e9f;

    /* Send LOAD_REQ as a framed UDP unicast */
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(DISCOVERY_PORT);
    inet_pton(AF_INET, ip, &dst.sin_addr);

    msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type        = MSG_LOAD_REQ;
    hdr.payload_len = 0;

    sendto(udp_fd, &hdr, sizeof(hdr), 0,
           (struct sockaddr *)&dst, sizeof(dst));

    /* Wait up to LOAD_QUERY_TIMEOUT_MS for LOAD_RESP */
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(udp_fd, &rfds);
    struct timeval tv = {
        .tv_sec  = LOAD_QUERY_TIMEOUT_MS / 1000,
        .tv_usec = (LOAD_QUERY_TIMEOUT_MS % 1000) * 1000
    };

    int sel = select(udp_fd + 1, &rfds, NULL, NULL, &tv);
    if (sel <= 0) {
        close(udp_fd);
        return 1e9f; /* timeout or error → treat as max load */
    }

    uint8_t buf[sizeof(msg_header_t) + sizeof(payload_load_resp_t) + 8];
    ssize_t n = recv(udp_fd, buf, sizeof(buf), 0);
    close(udp_fd);

    if (n < (ssize_t)(sizeof(msg_header_t) + sizeof(payload_load_resp_t)))
        return 1e9f;

    msg_header_t resp_hdr;
    memcpy(&resp_hdr, buf, sizeof(resp_hdr));
    if (resp_hdr.type != MSG_LOAD_RESP) return 1e9f;

    payload_load_resp_t resp;
    memcpy(&resp, buf + sizeof(msg_header_t), sizeof(resp));

    /* Update peer list */
    peer_list_update_load(ip, &resp);

    /* Decode float (network byte order) */
    uint32_t tmp; float f;
    memcpy(&tmp, &resp.load_pct, 4); tmp = ntohl(tmp); memcpy(&f, &tmp, 4);
    out_info->load_pct = f;

    out_info->active_tasks = ntohl(resp.active_tasks);
    out_info->total_tasks  = ntohl(resp.total_tasks);
    out_info->port = port;
    strncpy(out_info->ip, ip, INET_ADDRSTRLEN);


    return f + 10.0f * (float)out_info->active_tasks;

}

/* ─── Build and send MSG_TASK_FILE payload ───────────────────────────────── */
static int send_task_file(int fd, const task_t *task) {

    uint16_t fn_len   = (uint16_t)strlen(task->filename);
    uint32_t total    = 2 + fn_len + 4 + task->file_size;
    uint8_t *buf      = malloc(total);
    if (!buf) return -1;

    uint8_t *p = buf;
    uint16_t nfn = htons(fn_len);
    memcpy(p, &nfn, 2); p += 2;
    memcpy(p, task->filename, fn_len); p += fn_len;
    uint32_t nfs = htonl(task->file_size);
    memcpy(p, &nfs, 4); p += 4;
    memcpy(p, task->file_data, task->file_size);

    int ret = send_msg(fd, MSG_TASK_FILE, buf, total);
    free(buf);
    return ret;
}

/* ─── Receive and print streaming output until TASK_DONE ─────────────────── */
static int receive_output(int fd) {
    for (;;) {
        msg_type_t type;
        uint8_t   *payload = NULL;
        uint32_t   plen    = 0;

        if (recv_msg(fd, &type, &payload, &plen) < 0) {
            free(payload);
            return -1;
        }

        if (type == MSG_OUTPUT) {
            fwrite(payload, 1, plen, stdout);
            fflush(stdout);
        } else if (type == MSG_TASK_DONE) {
            int exit_code = 0;
            if (plen >= 4) {
                uint32_t ec;
                memcpy(&ec, payload, 4);
                exit_code = (int)(int32_t)ntohl(ec);
            }
            free(payload);
            return exit_code;
        }
        free(payload);
    }
}

/* ─── dispatch ───────────────────────────────────────────────────────────── */
int dispatch(const task_t *task, const char *local_ip, uint16_t own_port) {
    /* ── 1. Gather peers ── */
    peer_info_t peers[MAX_PEERS];
    int n_peers = peer_list_get_all(peers, MAX_PEERS);

    /* ── 2. Query each peer for load ── */
    float best_score      = 1e10f;
    char  best_ip[INET_ADDRSTRLEN]  = "";
    uint16_t best_port    = 0;
    int   use_local       = 0;

    for (int i = 0; i < n_peers; i++) {
        peer_info_t result;
        float score = query_peer_load(peers[i].ip, peers[i].port, &result);
        printf("  [load] %s:%u → load=%.1f%%\n",
               peers[i].ip, peers[i].port, result.load_pct);



        if (score < best_score) {
            best_score = score;
            strncpy(best_ip, peers[i].ip, INET_ADDRSTRLEN);
            best_port  = result.port;
        }
    }

    /* Also consider self */
    float self_score = load_score_local();
    printf("  [load] %s:%u (self) → load=%.1f%%\n",
           local_ip, own_port, load_get_percentage());




    if (n_peers == 0 || self_score <= best_score) {
        use_local = 1;
        printf("→ Routing task to SELF (%s:%u)\n", local_ip, own_port);
    } else {
        printf("→ Routing task to %s:%u (score=%.2f)\n",
               best_ip, best_port, best_score);
    }

    /* ── 3. Connect and send task ── */
    const char *target_ip   = use_local ? local_ip  : best_ip;
    uint16_t    target_port = use_local ? own_port  : best_port;

    int fd = tcp_connect(target_ip, target_port);
    if (fd < 0) {
        if (!use_local) {
            /* Fallback to self */
            fprintf(stderr, "Failed to connect to %s:%u, falling back to local\n",
                    target_ip, target_port);
            fd = tcp_connect(local_ip, own_port);
        }
        if (fd < 0) {
            fprintf(stderr, "dispatch: cannot connect anywhere\n");
            return -1;
        }
    }

    int send_ret = send_task_file(fd, task);


    if (send_ret < 0) {
        fprintf(stderr, "dispatch: failed to send task\n");
        close(fd);
        return -1;
    }

    /* ── 4. Stream output back ── */
    int exit_code = receive_output(fd);
    close(fd);
    return exit_code;
}
