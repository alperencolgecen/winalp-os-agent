#include "../include/prompt_engine.h"
#include "../include/logger.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>

#define MAX_TEMPLATES 64
#define MAX_PATH_LEN 1024

static char s_dir[MAX_PATH_LEN];
static char s_templates[MAX_TEMPLATES][256];
static char s_contents[MAX_TEMPLATES][16384];
static int  s_n;
static int  s_active; /* -1 = none */

static void load_templates(void) {
    s_n = 0;
    char search[MAX_PATH_LEN];
    snprintf(search, sizeof(search), "%s\\*.txt", s_dir);

    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (s_n >= MAX_TEMPLATES) break;
        /* name without .txt */
        const char *dot = strrchr(fd.cFileName, '.');
        int nlen = dot ? (int)(dot - fd.cFileName) : (int)strlen(fd.cFileName);
        if (nlen > 255) nlen = 255;
        strncpy(s_templates[s_n], fd.cFileName, (size_t)nlen);
        s_templates[s_n][nlen] = '\0';

        /* load content */
        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s\\%s", s_dir, fd.cFileName);
        FILE *f = fopen(path, "r");
        if (f) {
            size_t nr = fread(s_contents[s_n], 1, sizeof(s_contents[s_n]) - 1, f);
            s_contents[s_n][nr] = '\0';
            fclose(f);
        } else {
            s_contents[s_n][0] = '\0';
        }
        s_n++;
    } while (FindNextFile(h, &fd));
    FindClose(h);
    winalp_log(WINALP_LOG_INFO, "prompt_engine: loaded %d templates from %s", s_n, s_dir);
}

bool prompt_engine_init(const char *prompts_dir) {
    if (!prompts_dir) return false;
    strncpy(s_dir, prompts_dir, sizeof(s_dir) - 1);
    s_active = -1;
    load_templates();
    /* default: first template if none set */
    if (s_active < 0 && s_n > 0) s_active = 0;
    return true;
}

bool prompt_engine_set_template(const char *name) {
    if (!name) return false;
    for (int i = 0; i < s_n; i++) {
        if (strcmp(s_templates[i], name) == 0) {
            s_active = i;
            winalp_log(WINALP_LOG_INFO, "prompt_engine: active template: %s", name);
            return true;
        }
    }
    winalp_log(WINALP_LOG_WARN, "prompt_engine: template not found: %s", name);
    return false;
}

char *prompt_engine_build(const char *profile_summary, const char *plugin_guide) {
    /* core system prompt */
    const char *core = "You are WinAlp, a desktop AI assistant running entirely offline.\n"
                       "When performing file/OS actions produce ONLY a JSON action object "
                       "with this schema: {\"action\":\"...\",\"path\":\"...\",\"content\":\"...\"}\n";

    size_t total = strlen(core) + 1;
    if (profile_summary) total += strlen(profile_summary) + 20;
    if (plugin_guide)    total += strlen(plugin_guide) + 20;
    if (s_active >= 0)   total += strlen(s_contents[s_active]) + 20;

    char *out = (char*)malloc(total);
    if (!out) return NULL;
    out[0] = '\0';
    strcat(out, core);

    if (profile_summary) {
        strcat(out, "\n[Profile]\n");
        strcat(out, profile_summary);
    }
    if (plugin_guide) {
        strcat(out, "\n[Active Plugins]\n");
        strcat(out, plugin_guide);
    }
    if (s_active >= 0 && s_contents[s_active][0]) {
        strcat(out, "\n[Template]\n");
        strcat(out, s_contents[s_active]);
    }
    return out;
}

void prompt_engine_reload(void) {
    load_templates();
}

void prompt_engine_shutdown(void) {
    s_n = 0;
    s_active = -1;
}
