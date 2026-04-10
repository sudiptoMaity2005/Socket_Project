#ifndef DISPATCHER_H
#define DISPATCHER_H

#include "common.h"

/*
 * Dispatch a task to the least-loaded peer (or locally if none respond).
 * Returns the remote process exit code, or -1 on error.
 *
 * local_ip : this node's IP (for self-exclusion)
 * own_port : this node's TCP worker port
 */
int dispatch(const task_t *task, const char *local_ip, uint16_t own_port);

#endif /* DISPATCHER_H */
