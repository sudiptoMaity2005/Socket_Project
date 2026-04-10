#ifndef LOAD_MONITOR_H
#define LOAD_MONITOR_H

#include <stdint.h>

/* Get 1/5/15 minute load average. Returns 0 on success. */
int load_get(float *load1, float *load5, float *load15);

/* Return number of online CPU cores. */
uint32_t load_get_core_count(void);

/* Task counting for load formula */
void     load_task_start(void);
void     load_task_done(void);
uint32_t load_get_active_tasks(void);
uint32_t load_get_total_tasks(void);


/* Compute local load as a percentage (0-100) */
float    load_get_percentage(void);

/* Compute local score: load_pct + 10 * active_tasks (weight adjusted for pct) */
float    load_score_local(void);

#endif /* LOAD_MONITOR_H */

