#ifndef SYS_MONITOR_H
#define SYS_MONITOR_H

#include <stdbool.h>

typedef struct {
    float cpu_percent;                  /* total CPU usage (0-100) */
    unsigned long long ram_total_mb;
    unsigned long long ram_used_mb;
    unsigned long long disk_total_mb;   /* C: drive */
    unsigned long long disk_free_mb;
    unsigned long long uptime_sec;
    int battery_percent;                /* -1 = no battery */
    bool ac_power;                      /* true = plugged in */
    int hour, minute, second;
    char time_str[16];
    char date_str[16];
    int recycle_count;                  /* files in recycle bin */
    char ip_addr[32];                   /* local IP */
} SysMonitorData;

void sys_monitor_poll(SysMonitorData *d);

#endif
