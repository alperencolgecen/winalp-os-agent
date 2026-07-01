#include "../include/lua_runtime.h"
#include "../include/logger.h"
#include "../include/memory_store.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_SCRIPTS 64
#define MAX_PATH_LEN 1024
#define MAX_SANDBOX_DIR 512

/* ---- Sandbox helpers ---- */
static char s_last_error[4096];

/* ---- Hot-reload watcher ---- */
typedef struct {
    char path[MAX_PATH_LEN];
    __int64 last_write;
    bool    dirty;
} WatchedScript;

static WatchedScript s_scripts[MAX_SCRIPTS];
static int s_nscripts;
static char s_watch_dir[MAX_PATH_LEN];
static bool s_running;

/* Verify path stays within sandbox_data_dir */
static bool sandbox_verify_path(const char *sandbox_dir, const char *requested) {
    if (!sandbox_dir || !requested || !requested[0]) return false;
    /* Block traversal */
    if (strstr(requested, "..") || strchr(requested, ':') ||
        requested[0] == '/' || requested[0] == '\\')
        return false;
    /* Build full path and check it starts with sandbox_dir */
    char full[MAX_PATH_LEN];
    snprintf(full, sizeof(full), "%s\\%s", sandbox_dir, requested);
    /* Normalise separators */
    for (char *p = full; *p; p++) if (*p == '/') *p = '\\';
    return strncmp(full, sandbox_dir, strlen(sandbox_dir)) == 0;
}

/* ============ Lua C API bindings ============ */

/* store_get(key) → string or nil */
static int lua_store_get(lua_State *L) {
    const char *key = luaL_checkstring(L, 1);
    char val[1024];
    if (memory_store_get_profile(key, val, sizeof(val))) {
        lua_pushstring(L, val);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

/* store_set(key, value) */
static int lua_store_set(lua_State *L) {
    const char *key = luaL_checkstring(L, 1);
    const char *val = luaL_checkstring(L, 2);
    memory_store_set_profile(key, val);
    return 0;
}

/* log_info(msg) */
static int lua_log_info(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    winalp_log(WINALP_LOG_INFO, "lua: %s", msg);
    return 0;
}

/* file_read(path) → string or nil (sandboxed) */
static int lua_file_read(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    const char *sandbox = lua_tostring(L, lua_upvalueindex(1));
    if (!sandbox || !sandbox_verify_path(sandbox, path)) {
        lua_pushnil(L);
        return 1;
    }
    char full[MAX_PATH_LEN];
    snprintf(full, sizeof(full), "%s\\%s", sandbox, path);
    for (char *p = full; *p; p++) if (*p == '/') *p = '\\';

    HANDLE h = CreateFileA(full, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { lua_pushnil(L); return 1; }
    char buf[65536]; DWORD n;
    if (!ReadFile(h, buf, sizeof(buf) - 1, &n, NULL)) n = 0;
    buf[n] = '\0'; CloseHandle(h);
    lua_pushlstring(L, buf, (size_t)n);
    return 1;
}

/* file_write(path, content) → bool (sandboxed) */
static int lua_file_write(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    size_t len;
    const char *content = luaL_checklstring(L, 2, &len);
    const char *sandbox = lua_tostring(L, lua_upvalueindex(1));
    if (!sandbox || !sandbox_verify_path(sandbox, path)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    char full[MAX_PATH_LEN];
    snprintf(full, sizeof(full), "%s\\%s", sandbox, path);
    for (char *p = full; *p; p++) if (*p == '/') *p = '\\';

    HANDLE h = CreateFileA(full, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { lua_pushboolean(L, 0); return 1; }
    DWORD written;
    WriteFile(h, content, (DWORD)len, &written, NULL);
    CloseHandle(h);
    lua_pushboolean(L, 1);
    return 1;
}

/* file_delete(path) → bool (sandboxed) */
static int lua_file_delete(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    const char *sandbox = lua_tostring(L, lua_upvalueindex(1));
    if (!sandbox || !sandbox_verify_path(sandbox, path)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    char full[MAX_PATH_LEN];
    snprintf(full, sizeof(full), "%s\\%s", sandbox, path);
    for (char *p = full; *p; p++) if (*p == '/') *p = '\\';
    BOOL ok = DeleteFileA(full);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

/* Create a new sandboxed Lua state with WinAlp API */
lua_State *lua_runtime_new_state(const char *sandbox_data_dir) {
    lua_State *L = luaL_newstate();
    if (!L) return NULL;

    /* Open safe libraries */
    luaL_openlibs(L);

    /* Remove dangerous globals */
    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");
    lua_pushnil(L);
    lua_setglobal(L, "require");
    lua_pushnil(L);
    lua_setglobal(L, "package");

    /* Strip dangerous os.* functions */
    lua_getglobal(L, "os");
    if (!lua_isnil(L, -1)) {
        lua_pushnil(L); lua_setfield(L, -2, "execute");
        lua_pushnil(L); lua_setfield(L, -2, "exit");
        lua_pushnil(L); lua_setfield(L, -2, "tmpname");
        lua_pushnil(L); lua_setfield(L, -2, "rename");
        lua_pushnil(L); lua_setfield(L, -2, "remove");
    }
    lua_pop(L, 1);

    /* Strip dangerous io.* functions */
    lua_getglobal(L, "io");
    if (!lua_isnil(L, -1)) {
        lua_pushnil(L); lua_setfield(L, -2, "popen");
        lua_pushnil(L); lua_setfield(L, -2, "open");
    }
    lua_pop(L, 1);

    /* Register WinAlp API functions with sandbox dir as upvalue */
    if (sandbox_data_dir) {
        /* Ensure sandbox dir ends with backslash for prefix matching */
        char sb[MAX_SANDBOX_DIR];
        snprintf(sb, sizeof(sb), "%s", sandbox_data_dir);
        for (char *p = sb; *p; p++) if (*p == '/') *p = '\\';
        /* Remove trailing backslash if present */
        size_t slen = strlen(sb);
        if (slen > 0 && sb[slen - 1] == '\\') sb[slen - 1] = '\0';

        /* Register non-sandboxed functions first */
        lua_pushcclosure(L, lua_store_get, 0);
        lua_setglobal(L, "store_get");

        lua_pushcclosure(L, lua_store_set, 0);
        lua_setglobal(L, "store_set");

        lua_pushcclosure(L, lua_log_info, 0);
        lua_setglobal(L, "log_info");

        /* File APIs: push sandbox string as upvalue for each */
        lua_pushstring(L, sb);
        lua_pushcclosure(L, lua_file_read, 1);
        lua_setglobal(L, "file_read");

        lua_pushstring(L, sb);
        lua_pushcclosure(L, lua_file_write, 1);
        lua_setglobal(L, "file_write");

        lua_pushstring(L, sb);
        lua_pushcclosure(L, lua_file_delete, 1);
        lua_setglobal(L, "file_delete");
    }

    winalp_log(WINALP_LOG_INFO, "lua_runtime: new sandboxed state created (sandbox=%s)",
               sandbox_data_dir ? sandbox_data_dir : "(none)");
    return L;
}

/* Run a Lua script file */
bool lua_runtime_dofile(lua_State *L, const char *path) {
    if (!L || !path) return false;
    int ret = luaL_loadfile(L, path);
    if (ret != LUA_OK) {
        snprintf(s_last_error, sizeof(s_last_error), "lua: load error: %s",
                 lua_tostring(L, -1));
        winalp_log(WINALP_LOG_ERROR, "%s", s_last_error);
        lua_pop(L, 1);
        return false;
    }
    ret = lua_pcall(L, 0, 0, 0);
    if (ret != LUA_OK) {
        snprintf(s_last_error, sizeof(s_last_error), "lua: exec error: %s",
                 lua_tostring(L, -1));
        winalp_log(WINALP_LOG_ERROR, "%s", s_last_error);
        lua_pop(L, 1);
        return false;
    }
    return true;
}

/* Run a Lua string chunk */
bool lua_runtime_dostring(lua_State *L, const char *chunk) {
    if (!L || !chunk) return false;
    int ret = luaL_loadstring(L, chunk);
    if (ret != LUA_OK) {
        snprintf(s_last_error, sizeof(s_last_error), "lua: load error: %s",
                 lua_tostring(L, -1));
        winalp_log(WINALP_LOG_ERROR, "%s", s_last_error);
        lua_pop(L, 1);
        return false;
    }
    ret = lua_pcall(L, 0, 0, 0);
    if (ret != LUA_OK) {
        snprintf(s_last_error, sizeof(s_last_error), "lua: exec error: %s",
                 lua_tostring(L, -1));
        winalp_log(WINALP_LOG_ERROR, "%s", s_last_error);
        lua_pop(L, 1);
        return false;
    }
    return true;
}

const char *lua_runtime_dostring_result(lua_State *L, const char *chunk) {
    if (!L || !chunk) return NULL;
    if (luaL_loadstring(L, chunk) != LUA_OK) {
        snprintf(s_last_error, sizeof(s_last_error), "lua: load error: %s",
                 lua_tostring(L, -1));
        lua_pop(L, 1);
        return NULL;
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        snprintf(s_last_error, sizeof(s_last_error), "lua: exec error: %s",
                 lua_tostring(L, -1));
        lua_pop(L, 1);
        return NULL;
    }
    const char *s = lua_tostring(L, -1);
    if (!s) {
        lua_pop(L, 1);
        return NULL;
    }
    snprintf(s_last_error, sizeof(s_last_error), "%s", s);
    lua_pop(L, 1);
    return s_last_error;
}

/* Close a Lua state */
void lua_runtime_close(lua_State *L) {
    if (L) {
        lua_close(L);
        winalp_log(WINALP_LOG_INFO, "lua_runtime: state closed");
    }
}

/* ---- Hot-reload watcher (unchanged from stub) ---- */

bool lua_runtime_init(void) {
    s_running = true;
    s_nscripts = 0;
    s_last_error[0] = '\0';
    winalp_log(WINALP_LOG_INFO, "lua_runtime: initialised");
    return true;
}

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

void lua_runtime_watch(const char *scripts_dir) {
    if (!scripts_dir) return;
    strncpy(s_watch_dir, scripts_dir, MAX_PATH_LEN - 1);

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

        int idx = -1;
        for (int i = 0; i < s_nscripts; i++) {
            if (strcmp(s_scripts[i].path, full) == 0) { idx = i; break; }
        }
        if (idx >= 0) {
            if (s_scripts[idx].last_write != ft) {
                s_scripts[idx].last_write = ft;
                s_scripts[idx].dirty = true;
                winalp_log(WINALP_LOG_INFO, "lua_runtime: hot-reload: %s", full);
            }
        } else if (s_nscripts < MAX_SCRIPTS) {
            WatchedScript *ws = &s_scripts[s_nscripts++];
            strncpy(ws->path, full, MAX_PATH_LEN - 1);
            ws->last_write = ft;
            ws->dirty = true;
            winalp_log(WINALP_LOG_INFO, "lua_runtime: auto-tracked: %s", full);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

const char *lua_runtime_last_error(void) {
    return s_last_error;
}

void lua_runtime_shutdown(void) {
    s_running = false;
    s_nscripts = 0;
    winalp_log(WINALP_LOG_INFO, "lua_runtime: shut down");
}
