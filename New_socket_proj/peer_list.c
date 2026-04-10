#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "peer_list.h"
#include "common.h"
#include "load_monitor.h"


static peer_info_t g_peers[MAX_PEERS];
static pthread_rwlock_t g_lock = PTHREAD_RWLOCK_INITIALIZER;

/* ─── peer_list_init ─────────────────────────────────────────────────────── */
void peer_list_init(void) {
    pthread_rwlock_wrlock(&g_lock);
    memset(g_peers, 0, sizeof(g_peers));
    pthread_rwlock_unlock(&g_lock);
}

/* ─── find slot by IP (call with lock held) ──────────────────────────────── */
static int find_slot(const char *ip) {
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_peers[i].valid && strcmp(g_peers[i].ip, ip) == 0)
            return i;
    }
    return -1;
}

static int find_free_slot(void) {
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!g_peers[i].valid) return i;
    }
    return -1;
}

/* ─── peer_list_add ──────────────────────────────────────────────────────── */
void peer_list_add(const char *ip, uint16_t port) {
    pthread_rwlock_wrlock(&g_lock);
    int idx = find_slot(ip);
    if (idx < 0) {
        idx = find_free_slot();
        if (idx < 0) {
            pthread_rwlock_unlock(&g_lock);
            LOG("peer list full, cannot add %s", ip);
            return;
        }
        memset(&g_peers[idx], 0, sizeof(peer_info_t));
        strncpy(g_peers[idx].ip, ip, INET_ADDRSTRLEN - 1);
        g_peers[idx].valid     = 1;
        g_peers[idx].num_cores = 1; /* Default to 1 to avoid NaN before query */
        LOG("New peer discovered: %s:%u", ip, port);
    }
    g_peers[idx].port      = port;
    g_peers[idx].last_seen = time(NULL);
    pthread_rwlock_unlock(&g_lock);
}

/* ─── peer_list_remove ───────────────────────────────────────────────────── */
void peer_list_remove(const char *ip) {
    pthread_rwlock_wrlock(&g_lock);
    int idx = find_slot(ip);
    if (idx >= 0) {
        memset(&g_peers[idx], 0, sizeof(peer_info_t));
        LOG("Peer removed: %s", ip);
    }
    pthread_rwlock_unlock(&g_lock);
}

/* ─── peer_list_update_load ──────────────────────────────────────────────── */
void peer_list_update_load(const char *ip, const payload_load_resp_t *resp) {
    pthread_rwlock_wrlock(&g_lock);
    int idx = find_slot(ip);
    if (idx >= 0) {
        /* resp fields are in network byte order (float endian swap) */
        uint32_t tmp;
        float f;

        memcpy(&tmp, &resp->load_pct, 4); tmp = ntohl(tmp); memcpy(&f, &tmp, 4);
        g_peers[idx].load_pct = f;


        g_peers[idx].active_tasks = ntohl(resp->active_tasks);
        g_peers[idx].total_tasks  = ntohl(resp->total_tasks);
        g_peers[idx].num_cores    = ntohl(resp->num_cores);
        g_peers[idx].last_seen    = time(NULL);

    }
    pthread_rwlock_unlock(&g_lock);
}

/* ─── peer_list_touch ────────────────────────────────────────────────────── */
void peer_list_touch(const char *ip) {
    pthread_rwlock_wrlock(&g_lock);
    int idx = find_slot(ip);
    if (idx >= 0) g_peers[idx].last_seen = time(NULL);
    pthread_rwlock_unlock(&g_lock);
}

/* ─── peer_list_get_all ──────────────────────────────────────────────────── */
int peer_list_get_all(peer_info_t out[], int max) {
    pthread_rwlock_rdlock(&g_lock);
    int count = 0;
    for (int i = 0; i < MAX_PEERS && count < max; i++) {
        if (g_peers[i].valid) out[count++] = g_peers[i];
    }
    pthread_rwlock_unlock(&g_lock);
    return count;
}

/* ─── peer_list_reap_stale ───────────────────────────────────────────────── */
void peer_list_reap_stale(void) {
    time_t now = time(NULL);
    pthread_rwlock_wrlock(&g_lock);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_peers[i].valid &&
            (now - g_peers[i].last_seen) > PEER_TIMEOUT_SECS) {
            LOG("Peer timed out, removing: %s", g_peers[i].ip);
            memset(&g_peers[i], 0, sizeof(peer_info_t));
        }
    }
    pthread_rwlock_unlock(&g_lock);
}

/* ─── peer_list_print ────────────────────────────────────────────────────── */
void peer_list_print(const char *local_ip, uint16_t local_port) {
    pthread_rwlock_rdlock(&g_lock);
    printf("\n%-18s %-8s %-12s %-12s\n",
           "IP", "PORT", "LOAD (%)", "ACTIVE");
    printf("%-18s %-8s %-12s %-12s\n",
           "────────────────", "──────", "───────────", "───────────");

    /* 1. Print local node info */
    float local_load = load_get_percentage();
    uint32_t total_tasks = load_get_total_tasks();
    printf("%-18s %-8u %.1f%%        %u\n",
           local_ip, local_port, local_load, total_tasks);

    /* 2. Print discovered peers */
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_peers[i].valid) {
            printf("%-18s %-8u %.1f%%        %u\n",
                   g_peers[i].ip, g_peers[i].port,
                   g_peers[i].load_pct, g_peers[i].total_tasks);
        }
    }
    printf("\n");

    pthread_rwlock_unlock(&g_lock);
}

