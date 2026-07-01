#include "../include/sys_monitor.h"
#include "../include/logger.h"
#include <windows.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <string.h>

static ULONGLONG filetime_to_ull(FILETIME ft) {
    return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

void sys_monitor_poll(SysMonitorData *d) {
    if (!d) return;
    memset(d, 0, sizeof(*d));

    /* ── CPU via GetSystemTimes ── */
    {
        static FILETIME prev_idle, prev_kernel, prev_user;
        static bool first = true;
        FILETIME idle, kernel, user;
        if (GetSystemTimes(&idle, &kernel, &user)) {
            if (!first) {
                ULONGLONG idle_diff = filetime_to_ull(idle) - filetime_to_ull(prev_idle);
                ULONGLONG kernel_diff = filetime_to_ull(kernel) - filetime_to_ull(prev_kernel);
                ULONGLONG user_diff = filetime_to_ull(user) - filetime_to_ull(prev_user);
                ULONGLONG total = kernel_diff + user_diff;
                if (total > 0)
                    d->cpu_percent = (float)((total - idle_diff) * 100.0 / total);
                if (d->cpu_percent > 100.0f) d->cpu_percent = 100.0f;
                if (d->cpu_percent < 0.0f) d->cpu_percent = 0.0f;
            }
            prev_idle = idle; prev_kernel = kernel; prev_user = user;
            first = false;
        }
    }

    /* ── RAM ── */
    {
        MEMORYSTATUSEX ms;
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms)) {
            d->ram_total_mb = (unsigned long long)(ms.ullTotalPhys / (1024 * 1024));
            d->ram_used_mb  = (unsigned long long)((ms.ullTotalPhys - ms.ullAvailPhys) / (1024 * 1024));
        }
    }

    /* ── Disk C: ── */
    {
        ULARGE_INTEGER free_bytes, total_bytes;
        if (GetDiskFreeSpaceExA("C:\\", &free_bytes, &total_bytes, NULL)) {
            d->disk_total_mb = (unsigned long long)(total_bytes.QuadPart / (1024 * 1024));
            d->disk_free_mb  = (unsigned long long)(free_bytes.QuadPart / (1024 * 1024));
        }
    }

    /* ── Uptime ── */
    d->uptime_sec = GetTickCount64() / 1000;

    /* ── Battery ── */
    {
        SYSTEM_POWER_STATUS ps;
        if (GetSystemPowerStatus(&ps)) {
            d->ac_power = (ps.ACLineStatus == 1);
            d->battery_percent = (ps.BatteryLifePercent <= 100) ? (int)ps.BatteryLifePercent : -1;
        } else {
            d->battery_percent = -1;
        }
    }

    /* ── Time ── */
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        d->hour = st.wHour;
        d->minute = st.wMinute;
        d->second = st.wSecond;
        snprintf(d->time_str, sizeof(d->time_str), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        snprintf(d->date_str, sizeof(d->date_str), "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    }

    /* ── Recycle Bin ── */
    {
        typedef int (WINAPI *SHQueryRecycleBinAFn)(const char *, void *);
        HMODULE hSh = LoadLibraryA("shell32.dll");
        if (hSh) {
            SHQueryRecycleBinAFn qr = (SHQueryRecycleBinAFn)GetProcAddress(hSh, "SHQueryRecycleBinA");
            if (qr) {
                struct { unsigned long size, num_items; } rb = {0};
                if (qr("C:\\", &rb) == 0)
                    d->recycle_count = (int)rb.num_items;
            }
            FreeLibrary(hSh);
        }
    }

    /* ── Local IP via GetAdaptersInfo ── */
    {
        DWORD buflen = 0;
        if (GetAdaptersInfo(NULL, &buflen) == ERROR_BUFFER_OVERFLOW) {
            IP_ADAPTER_INFO *ai = (IP_ADAPTER_INFO*)malloc(buflen);
            if (ai && GetAdaptersInfo(ai, &buflen) == NO_ERROR) {
                IP_ADAPTER_INFO *p = ai;
                while (p) {
                    if (p->IpAddressList.IpAddress.String[0] &&
                        strcmp(p->IpAddressList.IpAddress.String, "0.0.0.0") != 0) {
                        strncpy(d->ip_addr, p->IpAddressList.IpAddress.String, sizeof(d->ip_addr) - 1);
                        break;
                    }
                    p = p->Next;
                }
            }
            free(ai);
        }
    }
}
