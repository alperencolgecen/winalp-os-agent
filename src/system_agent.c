#include "../include/system_agent.h"
#include "../include/logger.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SANDBOX_ROOT "work"
#define BUF_SZ 65536

static char s_buf[BUF_SZ];
static int  s_len;
static bool s_in_brace;
static int  s_brace_depth;

static ConfirmCallback s_confirm_cb;
static void           *s_confirm_ud;

void system_agent_set_confirm_cb(ConfirmCallback cb, void *ud) {
    s_confirm_cb = cb;
    s_confirm_ud = ud;
}

/* Minimal KV extractor: finds "key":" after key_name, copies value into out */
static int extract_str(const char *src, const char *key, char *out, int out_sz) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\" \":\" \"", key);
    /* simpler approach: just search for "key": " */
    char search[64];
    int slen = snprintf(search, sizeof(search), "\"%s\":\"", key);
    if (slen <= 0 || slen >= (int)sizeof(search)) return 0;
    const char *p = strstr(src, search);
    if (!p) return 0;
    p += slen;
    int i = 0;
    while (*p && *p != '"' && i < out_sz - 1) { out[i++] = *p++; }
    out[i] = '\0';
    return i;
}

static bool path_safe(const char *path) {
    if (!path || !path[0]) return false;
    if (strstr(path, "..") || strstr(path, "~") || strstr(path, ":"))
        return false;
    if (path[0] == '/' || path[0] == '\\') return false;
    return true;
}

static void exec_action(const char *json) {
    char action[64] = {0}, path[1024] = {0}, content[BUF_SZ] = {0};
    if (!extract_str(json, "action", action, sizeof(action))) return;
    extract_str(json, "path", path, sizeof(path));
    extract_str(json, "content", content, sizeof(content));

    if (!path_safe(path)) {
        winalp_log(WINALP_LOG_WARN, "agent: blocked unsafe path: %s", path);
        return;
    }

    char full[MAX_PATH];
    snprintf(full, sizeof(full), "%s\\%s\\%s", SANDBOX_ROOT, path[0] ? path : ".");

    if (strcmp(action, "create_file") == 0) {
        char dir[MAX_PATH];
        snprintf(dir, sizeof(dir), "%s\\%s", SANDBOX_ROOT, path);
        for (char *p = dir + strlen(SANDBOX_ROOT) + 1; *p; p++)
            if (*p == '\\') { *p = '\0'; CreateDirectory(dir, NULL); *p = '\\'; }
        FILE *f = fopen(full, "w");
        if (!f) { winalp_log(WINALP_LOG_ERROR, "agent: cannot write %s", full); return; }
        if (content[0]) fputs(content, f);
        fclose(f);
        winalp_log(WINALP_LOG_INFO, "agent: created %s", full);

    } else if (strcmp(action, "create_dir") == 0) {
        if (CreateDirectory(full, NULL) || GetLastError() == ERROR_ALREADY_EXISTS)
            winalp_log(WINALP_LOG_INFO, "agent: ensured dir %s", full);
        else
            winalp_log(WINALP_LOG_ERROR, "agent: cannot create dir %s", full);

    } else if (strcmp(action, "read_file") == 0) {
        FILE *f = fopen(full, "r");
        if (!f) { winalp_log(WINALP_LOG_WARN, "agent: cannot read %s", full); return; }
        char tmp[BUF_SZ]; size_t n = fread(tmp, 1, sizeof(tmp) - 1, f);
        tmp[n] = '\0'; fclose(f);
        winalp_log(WINALP_LOG_INFO, "agent: read %s (%zu bytes)", full, n);

    } else if (strcmp(action, "delete_file") == 0) {
        char desc[128]; snprintf(desc, sizeof(desc), "Delete file: %s", full);
        if (s_confirm_cb && !s_confirm_cb(desc, s_confirm_ud)) {
            winalp_log(WINALP_LOG_INFO, "agent: delete cancelled: %s", full);
            return;
        }
        if (DeleteFile(full)) winalp_log(WINALP_LOG_INFO, "agent: deleted %s", full);
        else winalp_log(WINALP_LOG_ERROR, "agent: delete failed %s", full);

    } else if (strcmp(action, "delete_dir") == 0) {
        char desc[128]; snprintf(desc, sizeof(desc), "Remove directory: %s", full);
        if (s_confirm_cb && !s_confirm_cb(desc, s_confirm_ud)) {
            winalp_log(WINALP_LOG_INFO, "agent: rmdir cancelled: %s", full);
            return;
        }
        if (RemoveDirectory(full)) winalp_log(WINALP_LOG_INFO, "agent: removed %s", full);
        else winalp_log(WINALP_LOG_ERROR, "agent: rmdir failed %s", full);

    } else if (strcmp(action, "update_profile") == 0) {
        winalp_log(WINALP_LOG_INFO, "agent: profile update: %s = %s", path, content);

    } else {
        winalp_log(WINALP_LOG_WARN, "agent: unknown action: %s", action);
    }
}

bool system_agent_feed(const char *token) {
    if (!token) return false;
    while (*token) {
        char c = *token++;
        if (c == '{' && !s_in_brace) {
            s_in_brace = true;
            s_brace_depth = 1;
            s_len = 0;
            s_buf[s_len++] = c;
        } else if (s_in_brace) {
            if (c == '{') s_brace_depth++;
            else if (c == '}') s_brace_depth--;
            if (s_len < BUF_SZ - 1) s_buf[s_len++] = c;
            if (s_brace_depth == 0) {
                s_buf[s_len] = '\0';
                s_in_brace = false;
                exec_action(s_buf);
                return true;
            }
        }
    }
    return false;
}

void system_agent_flush(void) {
    if (s_in_brace && s_len > 0) {
        s_buf[s_len] = '\0';
        winalp_log(WINALP_LOG_WARN, "agent: incomplete JSON at flush, discarding");
    }
    s_in_brace = false;
    s_len = 0;
    s_brace_depth = 0;
}
