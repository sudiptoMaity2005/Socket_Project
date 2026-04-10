#ifndef WORKER_H
#define WORKER_H

#include <stdint.h>

/*
 * Start the worker subsystem:
 * Binds a TCP listener on 'port' and spawns an accept-loop thread.
 * Each connection is handled in its own detached thread.
 */
void worker_start(uint16_t port);

#endif /* WORKER_H */
