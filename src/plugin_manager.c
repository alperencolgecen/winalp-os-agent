#include "../include/plugin_manager.h"
#include "../include/logger.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>

#define MAX_PLUGINS 64
#define MAX_PATH_LEN 1024

typedef struct {
    char name[128];
    char version[32];
    char author[128];
    char desc[512];
    char entry[256];
    char actions[1024];
    bool active;
    char dir[MAX_PATH_LEN];
} Plugin;

static char s_dir[MAX_PATH_LEN];
static Plugin s_plugins[MAX_PLUGINS];
static int s_n;

bool plugin_manager_init(const char *plugins_dir) {
    if (!plugins_dir) return false;
    strncpy(s_dir, plugins_dir, sizeof(s_dir) - 1);
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
        char tmp[1024];
        /* name */
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
        /* desc */
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
        /* actions */
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
            s_plugins[i].active = true;
            winalp_log(WINALP_LOG_INFO, "plugin_manager: activated: %s", name);
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

void plugin_manager_shutdown(void) {
    s_n = 0;
}
