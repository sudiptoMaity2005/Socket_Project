#ifndef LOAD_MONITOR_H
#define LOAD_MONITOR_H

#include <stdint.h>

/* Read /proc/loadavg and populate load averages; returns 0 on success */
int load_get(float *load1, float *load5, float *load15);

/* Thread-safe increment/decrement of the active task counter */
void load_task_start(void);
void load_task_done(void);

/* Return current active task count */
uint32_t load_get_active_tasks(void);

/*
 * Composite load score for scheduling decisions.
 * Higher = more loaded. Formula: load1 + 0.5 * active_tasks
 */
float load_score_local(void);

#endif /* LOAD_MONITOR_H */
