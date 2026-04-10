#ifndef DISCOVERY_H
#define DISCOVERY_H

/*
 * Start the discovery subsystem:
 * - Announcer thread: broadcasts MSG_HELLO every DISCOVERY_INTERVAL seconds
 * - Listener thread: receives UDP packets and updates the peer list
 *
 * own_tcp_port : the TCP port this node's worker listens on
 * local_ip     : this node's IP (for self-exclusion)
 */
void discovery_start(uint16_t own_tcp_port, const char *local_ip);

/* Broadcast MSG_BYE before shutdown */
void discovery_send_bye(void);

#endif /* DISCOVERY_H */
