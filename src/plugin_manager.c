#include "../include/plugin_manager.h"
#include "../include/logger.h"
#include "../include/memory_store.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>

#define MAX_PLUGINS 64
#define MAX_PATH_LEN 1024

/* Blacklisted function name fragments */
static const char *s_blacklist[] = {
    "CreateProcess", "system", "popen", "exec", "fork",
    "DeleteFile", "RemoveDirectory", "CreateFile",
    "RegOpenKey", "RegSetValue", "socket", "connect",
    NULL
};

typedef struct {
    char name[128];
    char version[32];
    char author[128];
    char desc[512];
    char entry[256];
    char actions[1024];
    char data_dir[MAX_PATH_LEN]; /* sandboxed data directory */
    bool active;
    char dir[MAX_PATH_LEN];
    __int64 last_write;          /* for hot-reload detection */
    __int64 meta_write;
} Plugin;

static char s_dir[MAX_PATH_LEN];
static Plugin s_plugins[MAX_PLUGINS];
static int s_n;

static PluginActionCallback s_action_cb;
static void *s_action_ud;

/* Blacklist check */
static bool is_blacklisted(const char *func_name) {
    if (!func_name) return true;
    for (int i = 0; s_blacklist[i]; i++) {
        if (strstr(func_name, s_blacklist[i])) return true;
    }
    return false;
}

/* Ensure sandbox data dir exists */
static bool ensure_sandbox_dir(const char *plugin_name) {
    char data_dir[MAX_PATH_LEN];
    snprintf(data_dir, sizeof(data_dir), "profile/plugins/%s", plugin_name);

    char tmp[MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "profile\\plugins"); CreateDirectoryA(tmp, NULL);
    snprintf(tmp, sizeof(tmp), "%s", data_dir);
    for (char *p = tmp; *p; p++) if (*p == '/') *p = '\\';
    CreateDirectoryA(tmp, NULL);

    snprintf(tmp, sizeof(tmp), "%s\\data", data_dir);
    for (char *p = tmp; *p; p++) if (*p == '/') *p = '\\';
    CreateDirectoryA(tmp, NULL);

    return true;
}

void plugin_manager_set_action_cb(PluginActionCallback cb, void *ud) {
    s_action_cb = cb;
    s_action_ud = ud;
}

bool plugin_manager_init(const char *plugins_dir) {
    if (!plugins_dir) return false;
    strncpy(s_dir, plugins_dir, sizeof(s_dir) - 1);

    /* Ensure plugins/ root dir exists */
    CreateDirectoryA(s_dir, NULL);
    return true;
}

int plugin_manager_scan(void) {
    s_n = 0;
    char search[MAX_PATH_LEN];
    snprintf(search, sizeof(search), "%s\\*", s_dir);

    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (s_n >= MAX_PLUGINS) break;

        char meta_path[MAX_PATH_LEN];
        snprintf(meta_path, sizeof(meta_path), "%s\\%s\\plugin.json", s_dir, fd.cFileName);

        FILE *f = fopen(meta_path, "r");
        if (!f) continue;

        Plugin *p = &s_plugins[s_n];
        memset(p, 0, sizeof(Plugin));
        strncpy(p->dir, fd.cFileName, sizeof(p->dir) - 1);

        char buf[4096];
        size_t nr = fread(buf, 1, sizeof(buf) - 1, f);
        buf[nr] = '\0'; fclose(f);

        /* Minimal JSON field extraction */
        const char *nstart = strstr(buf, "\"name\"");
        if (nstart) {
            nstart = strchr(nstart + 6, '"');
            if (nstart) {
                nstart++;
                int i = 0;
                while (*nstart && *nstart != '"' && i < (int)sizeof(p->name) - 1)
                    p->name[i++] = *nstart++;
                p->name[i] = '\0';
            }
        }
        nstart = strstr(buf, "\"description\"");
        if (nstart) {
            nstart = strchr(nstart + 12, '"');
            if (nstart) {
                nstart++;
                int i = 0;
                while (*nstart && *nstart != '"' && i < (int)sizeof(p->desc) - 1)
                    p->desc[i++] = *nstart++;
                p->desc[i] = '\0';
            }
        }
        nstart = strstr(buf, "\"actions\"");
        if (nstart) {
            nstart = strchr(nstart + 8, '[');
            if (nstart) {
                nstart++;
                int i = 0;
                while (*nstart && *nstart != ']' && i < (int)sizeof(p->actions) - 1)
                    p->actions[i++] = *nstart++;
                p->actions[i] = '\0';
            }
        }

        p->active = false;
        snprintf(p->entry, sizeof(p->entry), "%s\\%s\\main.lua", s_dir, fd.cFileName);
        snprintf(p->data_dir, sizeof(p->data_dir), "profile/plugins/%s/data", p->name);

        /* Verify actions against blacklist */
        if (p->actions[0]) {
            char actions_copy[1024];
            strncpy(actions_copy, p->actions, sizeof(actions_copy) - 1);
            char *tok = strtok(actions_copy, " ,\"\n\r\t");
            while (tok) {
                if (is_blacklisted(tok)) {
                    winalp_log(WINALP_LOG_WARN,
                               "plugin_manager: blacklisted action '%s' in %s — skipping",
                               tok, fd.cFileName);
                    memset(p, 0, sizeof(Plugin));
                    break;
                }
                tok = strtok(NULL, " ,\"\n\r\t");
            }
            if (!p->name[0]) continue; /* skip invalid plugin */
        }

        s_n++;
    } while (FindNextFile(h, &fd));
    FindClose(h);

    winalp_log(WINALP_LOG_INFO, "plugin_manager: scanned %d plugins from %s", s_n, s_dir);
    return s_n;
}

bool plugin_manager_activate(const char *name) {
    if (!name) return false;
    for (int i = 0; i < s_n; i++) {
        if (strcmp(s_plugins[i].name, name) == 0) {
            ensure_sandbox_dir(s_plugins[i].name);
            s_plugins[i].active = true;
            winalp_log(WINALP_LOG_INFO, "plugin_manager: activated: %s (sandbox: %s)",
                       name, s_plugins[i].data_dir);
            return true;
        }
    }
    return false;
}

bool plugin_manager_deactivate(const char *name) {
    if (!name) return false;
    for (int i = 0; i < s_n; i++) {
        if (strcmp(s_plugins[i].name, name) == 0) {
            s_plugins[i].active = false;
            winalp_log(WINALP_LOG_INFO, "plugin_manager: deactivated: %s", name);
            return true;
        }
    }
    return false;
}

bool plugin_manager_is_action_allowed(const char *action) {
    return !is_blacklisted(action);
}

char *plugin_manager_build_guide(void) {
    size_t total = 1;
    for (int i = 0; i < s_n; i++) {
        if (!s_plugins[i].active) continue;
        total += strlen(s_plugins[i].name) + strlen(s_plugins[i].desc) + 10;
        total += strlen(s_plugins[i].actions) + 5;
    }
    char *out = (char*)malloc(total);
    if (!out) return NULL;
    out[0] = '\0';
    for (int i = 0; i < s_n; i++) {
        if (!s_plugins[i].active) continue;
        char line[2048];
        snprintf(line, sizeof(line), "- %s: %s [actions: %s]\n",
                 s_plugins[i].name, s_plugins[i].desc, s_plugins[i].actions);
        strcat(out, line);
    }
    if (!out[0]) strcat(out, "(none active)\n");
    return out;
}

/* Hot-reload: poll each plugin's plugin.json and main.lua for changes */
int plugin_manager_watch(void) {
    int reloaded = 0;
    for (int i = 0; i < s_n; i++) {
        /* Check plugin.json */
        char meta_path[MAX_PATH_LEN];
        snprintf(meta_path, sizeof(meta_path), "%s\\%s\\plugin.json", s_dir, s_plugins[i].dir);

        WIN32_FILE_ATTRIBUTE_DATA info;
        if (GetFileAttributesExA(meta_path, GetFileExInfoStandard, &info)) {
            __int64 mtime = ((__int64)info.ftLastWriteTime.dwHighDateTime << 32)
                          | (__int64)info.ftLastWriteTime.dwLowDateTime;
            if (mtime != s_plugins[i].meta_write) {
                s_plugins[i].meta_write = mtime;
                /* Reload this plugin */
                FILE *f = fopen(meta_path, "r");
                if (f) {
                    char buf[4096]; size_t nr = fread(buf, 1, sizeof(buf) - 1, f);
                    buf[nr] = '\0'; fclose(f);

                    const char *nstart = strstr(buf, "\"actions\"");
                    if (nstart) {
                        nstart = strchr(nstart + 8, '[');
                        if (nstart) {
                            nstart++;
                            int ai = 0; char new_actions[1024] = "";
                            while (*nstart && *nstart != ']' && ai < (int)sizeof(new_actions) - 1)
                                new_actions[ai++] = *nstart++;
                            new_actions[ai] = '\0';
                            /* Notify about removed actions */
                            if (s_action_cb && s_plugins[i].actions[0]) {
                                char *tok = strtok(s_plugins[i].actions, " ,\"\n\r\t");
                                while (tok) {
                                    if (!strstr(new_actions, tok))
                                        s_action_cb(tok, false, s_action_ud);
                                    tok = strtok(NULL, " ,\"\n\r\t");
                                }
                            }
                            strncpy(s_plugins[i].actions, new_actions, sizeof(s_plugins[i].actions) - 1);
                            /* Notify about added actions */
                            if (s_action_cb) {
                                char copy[1024]; strncpy(copy, new_actions, sizeof(copy) - 1);
                                char *tok = strtok(copy, " ,\"\n\r\t");
                                while (tok) {
                                    s_action_cb(tok, true, s_action_ud);
                                    tok = strtok(NULL, " ,\"\n\r\t");
                                }
                            }
                            winalp_log(WINALP_LOG_INFO,
                                       "plugin_manager: hot-reloaded %s", s_plugins[i].name);
                            reloaded++;
                        }
                    }
                }
            }
        }

        /* Check main.lua */
        WIN32_FILE_ATTRIBUTE_DATA linfo;
        if (GetFileAttributesExA(s_plugins[i].entry, GetFileExInfoStandard, &linfo)) {
            __int64 lt = ((__int64)linfo.ftLastWriteTime.dwHighDateTime << 32)
                       | (__int64)linfo.ftLastWriteTime.dwLowDateTime;
            if (lt != s_plugins[i].last_write) {
                s_plugins[i].last_write = lt;
                winalp_log(WINALP_LOG_INFO, "plugin_manager: script changed: %s",
                           s_plugins[i].entry);
                reloaded++;
            }
        }
    }
    return reloaded;
}

/* Build comma-separated list of all registered actions from active plugins */
char *plugin_manager_get_actions(void) {
    size_t total = 1;
    for (int i = 0; i < s_n; i++) {
        if (!s_plugins[i].active) continue;
        total += strlen(s_plugins[i].actions) + 2;
    }
    char *out = (char*)malloc(total);
    if (!out) return NULL;
    out[0] = '\0';
    for (int i = 0; i < s_n; i++) {
        if (!s_plugins[i].active) continue;
        if (out[0]) strcat(out, ",");
        strcat(out, s_plugins[i].actions);
    }
    return out;
}

void plugin_manager_shutdown(void) {
    s_n = 0;
    s_action_cb = NULL;
    s_action_ud = NULL;
}
