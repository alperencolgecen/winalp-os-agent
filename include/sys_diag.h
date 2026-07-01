#ifndef SYS_DIAG_H
#define SYS_DIAG_H

#include <stdbool.h>

typedef struct {
    char cpu_name[128];
    int  cpu_cores;
    int  cpu_threads;
    unsigned long long ram_total_mb;
    unsigned long long ram_free_mb;
    unsigned long long vram_total_mb;
    unsigned long long vram_free_mb;
    char gpu_name[256];
    int  model_tier; /* 0=unknown, 1=light, 2=medium, 3=heavy */
} SysDiag;

bool sys_diag_detect(SysDiag *d);
void sys_diag_print(const SysDiag *d);

#endif
