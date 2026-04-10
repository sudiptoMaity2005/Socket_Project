#ifndef NET_UTILS_H
#define NET_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include "common.h"

/* Send exactly 'len' bytes from buf over fd; returns -1 on error */
int send_all(int fd, const void *buf, size_t len);

/* Receive exactly 'len' bytes into buf from fd; returns -1 on error/EOF */
int recv_all(int fd, void *buf, size_t len);

/* Send a framed message (header + payload) over fd */
int send_msg(int fd, msg_type_t type, const void *payload, uint32_t payload_len);

/*
 * Receive a framed message from fd.
 * Allocates *payload_out on heap (caller must free).
 * Returns 0 on success, -1 on error/EOF.
 */
int recv_msg(int fd, msg_type_t *type_out, uint8_t **payload_out, uint32_t *len_out);

/*
 * Get the primary IP address of the given network interface.
 * Falls back to first non-loopback if iface is NULL or not found.
 * Returns 0 on success, -1 on failure.
 */
int get_local_ip(const char *iface, char out_ip[INET_ADDRSTRLEN]);

/* Create a UDP socket with SO_BROADCAST set; bind to port (0 = any) */
int make_udp_broadcast_socket(int port);

/* Create a TCP listening socket bound to port; returns fd or -1 */
int make_tcp_server(int port);

/* Connect TCP to ip:port; returns connected fd or -1 */
int tcp_connect(const char *ip, uint16_t port);

#endif /* NET_UTILS_H */
