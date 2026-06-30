#include "../include/context_tracker.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

void context_tracker_poll(char *out_label, int out_len) {
    if (!out_label || out_len <= 0) return;
    out_label[0] = '\0';

    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return;

    char title[256];
    int n = GetWindowTextA(hwnd, title, sizeof(title));
    if (n <= 0) return;
    title[n] = '\0';

    /* Get executable name */
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    char exe[128] = {0};
    if (hProc) {
        DWORD sz = sizeof(exe);
        QueryFullProcessImageNameA(hProc, 0, exe, &sz);
        CloseHandle(hProc);
        /* extract filename from path */
        char *p = strrchr(exe, '\\');
        if (p) memmove(exe, p + 1, strlen(p));
    }

    if (exe[0])
        snprintf(out_label, (size_t)out_len, "%s — %s", exe, title);
    else
        strncpy(out_label, title, (size_t)out_len - 1);
    out_label[out_len - 1] = '\0';
}
