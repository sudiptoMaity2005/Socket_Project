#ifndef PEER_LIST_H
#define PEER_LIST_H

#include "common.h"

/* Initialize the peer list; call once at startup */
void peer_list_init(void);

/* Add or update a peer (upsert by IP) */
void peer_list_add(const char *ip, uint16_t port);

/* Remove a peer by IP */
void peer_list_remove(const char *ip);

/* Update load info for a known peer */
void peer_list_update_load(const char *ip, const payload_load_resp_t *resp);

/*
 * Snapshot all valid peers into out[] (caller provides array of size max).
 * Returns number of peers copied.
 */
int peer_list_get_all(peer_info_t out[], int max);

/* Update last_seen for a peer (e.g. on HELLO heartbeat) */
void peer_list_touch(const char *ip);

/* Reaper: remove peers not seen for PEER_TIMEOUT_SECS; call periodically */
void peer_list_reap_stale(void);

/* Print peer list to stdout (for CLI 'peers' command) */
void peer_list_print(void);

#endif /* PEER_LIST_H */
