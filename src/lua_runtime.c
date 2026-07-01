#include "../include/lua_runtime.h"
#include "../include/logger.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_SCRIPTS 64
#define MAX_PATH_LEN 1024

typedef struct {
    char path[MAX_PATH_LEN];
    __int64 last_write;
    bool    dirty;
} WatchedScript;

static WatchedScript s_scripts[MAX_SCRIPTS];
static int s_nscripts;
static char s_watch_dir[MAX_PATH_LEN];
static bool s_running;

bool lua_runtime_init(void) {
    s_running = true;
    s_nscripts = 0;
    winalp_log(WINALP_LOG_INFO, "lua_runtime: initialised (Lua engine stub)");
    return true;
}

/* Add a script to the watch list */
bool lua_runtime_exec_file(const char *path) {
    if (!path || s_nscripts >= MAX_SCRIPTS) return false;

    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) {
        winalp_log(WINALP_LOG_WARN, "lua_runtime: script not found: %s", path);
        return false;
    }

    WatchedScript *ws = &s_scripts[s_nscripts++];
    strncpy(ws->path, path, MAX_PATH_LEN - 1);
    ws->last_write = ((__int64)info.ftLastWriteTime.dwHighDateTime << 32)
                   | (__int64)info.ftLastWriteTime.dwLowDateTime;
    ws->dirty = true;

    winalp_log(WINALP_LOG_INFO, "lua_runtime: tracked script: %s", path);
    return true;
}

/* Poll watched directory for changes — call each frame */
void lua_runtime_watch(const char *scripts_dir) {
    if (!scripts_dir) return;
    strncpy(s_watch_dir, scripts_dir, MAX_PATH_LEN - 1);

    /* Re-scan scripts/ on each poll — new files appear dynamically */
    char search[MAX_PATH_LEN];
    snprintf(search, sizeof(search), "%s\\*.lua", s_watch_dir);

    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFileA(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        char full[MAX_PATH_LEN];
        snprintf(full, sizeof(full), "%s\\%s", s_watch_dir, fd.cFileName);

        __int64 ft = ((__int64)fd.ftLastWriteTime.dwHighDateTime << 32)
                   | (__int64)fd.ftLastWriteTime.dwLowDateTime;

        /* Check if already tracked */
        int idx = -1;
        for (int i = 0; i < s_nscripts; i++) {
            if (strcmp(s_scripts[i].path, full) == 0) {
                idx = i;
                break;
            }
        }
        if (idx >= 0) {
            if (s_scripts[idx].last_write != ft) {
                s_scripts[idx].last_write = ft;
                s_scripts[idx].dirty = true;
                winalp_log(WINALP_LOG_INFO, "lua_runtime: hot-reload detected: %s", full);
            }
        } else if (s_nscripts < MAX_SCRIPTS) {
            /* Auto-track new scripts */
            WatchedScript *ws = &s_scripts[s_nscripts++];
            strncpy(ws->path, full, MAX_PATH_LEN - 1);
            ws->last_write = ft;
            ws->dirty = true;
            winalp_log(WINALP_LOG_INFO, "lua_runtime: auto-tracked: %s", full);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

void lua_runtime_shutdown(void) {
    s_running = false;
    s_nscripts = 0;
    winalp_log(WINALP_LOG_INFO, "lua_runtime: shut down");
}
