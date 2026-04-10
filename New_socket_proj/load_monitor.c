#include <stdio.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "load_monitor.h"
#include "common.h"

static atomic_uint g_active_tasks = 0;

/* ─── load_get ───────────────────────────────────────────────────────────── */
int load_get(float *load1, float *load5, float *load15) {
#ifdef __linux__
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) {
        perror("fopen /proc/loadavg");
        *load1 = *load5 = *load15 = 99.0f;
        return -1;
    }
    int ret = fscanf(f, "%f %f %f", load1, load5, load15);
    fclose(f);
    if (ret != 3) {
        fprintf(stderr, "load_get: unexpected /proc/loadavg format\n");
        *load1 = *load5 = *load15 = 99.0f;
        return -1;
    }
    return 0;
#else
    double load[3];
    if (getloadavg(load, 3) != -1) {
        *load1  = (float)load[0];
        *load5  = (float)load[1];
        *load15 = (float)load[2];
        return 0;
    }
    perror("getloadavg");
    *load1 = *load5 = *load15 = 99.0f;
    return -1;
#endif
}

/* ─── active task counter ────────────────────────────────────────────────── */
void load_task_start(void) { atomic_fetch_add(&g_active_tasks, 1); }
void load_task_done(void)  { atomic_fetch_sub(&g_active_tasks, 1); }

uint32_t load_get_active_tasks(void) {
    return (uint32_t)atomic_load(&g_active_tasks);
}

/* ─── load_score_local ───────────────────────────────────────────────────── */
float load_score_local(void) {
    float l1 = 0.0f, l5 = 0.0f, l15 = 0.0f;
    load_get(&l1, &l5, &l15);
    return l1 + 0.5f * (float)load_get_active_tasks();
}
