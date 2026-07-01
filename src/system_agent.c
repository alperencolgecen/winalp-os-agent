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

static ConfirmCallback    s_confirm_cb;
static void             *s_confirm_ud;
static FileContentCallback  s_content_cb;
static void             *s_content_ud;
static ReasoningCallback    s_reason_cb;
static void               *s_reason_ud;
static PluginActionCallback s_plugin_cb;
static void               *s_plugin_ud;

/* <think> tag parser state */
enum { THINK_IDLE, THINK_OPENING, THINK_BODY, THINK_CLOSING };
static int  s_think_state;
static char s_think_buf[16384];
static int  s_think_len;
static int  s_tag_pos; /* position inside <think> or </think> */

void system_agent_set_confirm_cb(ConfirmCallback cb, void *ud) {
    s_confirm_cb = cb;
    s_confirm_ud = ud;
}

void system_agent_set_content_cb(FileContentCallback cb, void *ud) {
    s_content_cb = cb;
    s_content_ud = ud;
}

void system_agent_set_reasoning_cb(ReasoningCallback cb, void *ud) {
    s_reason_cb = cb;
    s_reason_ud = ud;
}

void system_agent_set_plugin_action_cb(PluginActionCallback cb, void *ud) {
    s_plugin_cb = cb;
    s_plugin_ud = ud;
}

/* JSON tokeniser states */
enum { J_INIT, J_IN_KEY, J_IN_STR, J_IN_INT, J_DONE };

/* Extract value for a given key from a JSON object, regex-free state machine */
static int extract_str(const char *src, const char *key, char *out, int out_sz) {
    if (!src || !key) return 0;
    out[0] = '\0';
    int state = J_INIT, ki = 0;
    char cur_key[64];
    int  cur_key_len = 0;

    for (const char *p = src; *p && state != J_DONE; p++) {
        char c = *p;
        switch (state) {
        case J_INIT:
            if (c == '{') state = J_IN_KEY, cur_key_len = 0;
            break;
        case J_IN_KEY:
            if (c == '"' && (p == src || *(p-1) != '\\')) {
                /* end of key string */
                cur_key[cur_key_len] = '\0';
                /* skip whitespace and colon */
                p++;
                while (*p && (*p == ' ' || *p == ':')) p++;
                if (!*p) return 0;
                if (*p == '"') { state = J_IN_STR; ki = 0; }
                else if (*p == '{' || *p == '[') {
                    /* skip nested object/array */
                    int depth = 1; p++;
                    while (*p && depth > 0) {
                        if (*p == '{' || *p == '[') depth++;
                        else if (*p == '}' || *p == ']') depth--;
                        p++;
                    }
                    if (!*p) return 0;
                    state = J_IN_KEY; cur_key_len = 0;
                } else { state = J_IN_INT; ki = 0; }
                /* check if this is our target key */
                if (strcmp(cur_key, key) != 0) {
                    /* skip value */
                    if (state == J_IN_STR) {
                        while (*p && !(*p == '"' && *(p-1) != '\\')) p++;
                    } else if (state == J_IN_INT) {
                        while (*p && *p != ',' && *p != '}' && *p != ' ') p++;
                    }
                    state = J_IN_KEY; cur_key_len = 0;
                    if (*p) p--;
                }
                continue;
            }
            if (cur_key_len < (int)sizeof(cur_key) - 1)
                cur_key[cur_key_len++] = c;
            break;
        case J_IN_STR:
            if (c == '"' && (p == src || *(p-1) != '\\')) {
                out[ki] = '\0';
                state = J_DONE;
            } else if (ki < out_sz - 1) {
                out[ki++] = c;
            }
            break;
        case J_IN_INT:
            if (c == ',' || c == '}' || c == ' ') {
                out[ki] = '\0';
                state = J_DONE;
            } else if (ki < out_sz - 1) {
                out[ki++] = c;
            }
            break;
        default: break;
        }
    }
    return (int)strlen(out);
}

static const char *s_allowed_roots[] = {
    "work", "profile", "scripts", "prompts", "plugins", NULL
};

static bool path_verify(const char *path) {
    if (!path || !path[0]) return false;
    /* block traversal chars and absolute paths */
    if (strstr(path, "..") || strstr(path, "~") || strchr(path, ':'))
        return false;
    if (path[0] == '/' || path[0] == '\\') return false;
    if (strchr(path, '*') || strchr(path, '?') || strchr(path, '<') ||
        strchr(path, '>') || strchr(path, '|'))
        return false;
    /* path must start with one of the allowed roots */
    for (int i = 0; s_allowed_roots[i]; i++) {
        size_t rlen = strlen(s_allowed_roots[i]);
        if (strncmp(path, s_allowed_roots[i], rlen) == 0) {
            char next = path[rlen];
            if (next == '\\' || next == '/' || next == '\0')
                return true;
        }
    }
    return false;
}

static void exec_action(const char *json) {
    char action[64] = {0}, path[1024] = {0}, content[BUF_SZ] = {0};
    if (!extract_str(json, "action", action, sizeof(action))) return;
    extract_str(json, "path", path, sizeof(path));
    extract_str(json, "content", content, sizeof(content));

    if (!path_verify(path)) {
        winalp_log(WINALP_LOG_WARN, "agent: blocked path outside whitelist: %s", path);
        return;
    }

    char full[MAX_PATH];
    snprintf(full, sizeof(full), "%s\\%s", SANDBOX_ROOT, path);

    if (strcmp(action, "create_file") == 0) {
        /* ensure parent dirs via Windows API */
        char dir[MAX_PATH];
        snprintf(dir, sizeof(dir), "%s\\%s", SANDBOX_ROOT, path);
        for (char *p = dir + strlen(SANDBOX_ROOT) + 1; *p; p++)
            if (*p == '\\') { *p = '\0'; CreateDirectoryA(dir, NULL); *p = '\\'; }
        /* write file via CreateFile/WriteFile */
        HANDLE hFile = CreateFileA(full, GENERIC_WRITE, 0, NULL,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            winalp_log(WINALP_LOG_ERROR, "agent: cannot write %s", full);
            return;
        }
        DWORD written;
        WriteFile(hFile, content, (DWORD)strlen(content), &written, NULL);
        CloseHandle(hFile);
        winalp_log(WINALP_LOG_INFO, "agent: created %s (%lu bytes)", full, written);

    } else if (strcmp(action, "create_dir") == 0) {
        if (CreateDirectory(full, NULL) || GetLastError() == ERROR_ALREADY_EXISTS)
            winalp_log(WINALP_LOG_INFO, "agent: ensured dir %s", full);
        else
            winalp_log(WINALP_LOG_ERROR, "agent: cannot create dir %s", full);

    } else if (strcmp(action, "read_file") == 0) {
        char desc[128]; snprintf(desc, sizeof(desc), "Read file: %s", full);
        if (s_confirm_cb && !s_confirm_cb(desc, s_confirm_ud)) {
            winalp_log(WINALP_LOG_INFO, "agent: read cancelled: %s", full);
            return;
        }
        HANDLE hFile = CreateFileA(full, GENERIC_READ, FILE_SHARE_READ, NULL,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            winalp_log(WINALP_LOG_WARN, "agent: cannot read %s", full);
            return;
        }
        char tmp[BUF_SZ]; DWORD n;
        if (!ReadFile(hFile, tmp, BUF_SZ - 1, &n, NULL)) n = 0;
        tmp[n] = '\0'; CloseHandle(hFile);
        winalp_log(WINALP_LOG_INFO, "agent: read %s (%lu bytes)", full, n);
        if (s_content_cb) s_content_cb(tmp, s_content_ud);

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

    } else if (s_plugin_cb && s_plugin_cb(action, path, content, s_plugin_ud)) {
        winalp_log(WINALP_LOG_INFO, "agent: plugin handled action: %s", action);
    } else {
        winalp_log(WINALP_LOG_WARN, "agent: unknown action: %s", action);
    }
}

/* Feed a token (word fragment) through the <think> tag FSM and JSON parser */
bool system_agent_feed(const char *token) {
    if (!token) return false;

    /* Process character by character */
    while (*token) {
        char c = *token++;

        /* <think> tag state machine */
        switch (s_think_state) {
        case THINK_IDLE:
            if (c == '<') {
                s_think_state = THINK_OPENING;
                s_tag_pos = 0;
                continue;
            }
            break;
        case THINK_OPENING: {
            const char *tag = "think>";
            if (s_tag_pos < 6 && c == tag[s_tag_pos]) {
                s_tag_pos++;
                if (s_tag_pos == 6) {
                    s_think_state = THINK_BODY;
                    s_think_len = 0;
                }
            } else {
                /* False alarm: emit skipped '<' and current char to JSON parser */
                s_think_state = THINK_IDLE;
                /* re-route '<' and c through the JSON parser */
                goto emit_json;
            }
            continue;
        }
        case THINK_BODY:
            if (c == '<') {
                s_think_state = THINK_CLOSING;
                s_tag_pos = 0;
                continue;
            }
            if (s_think_len < (int)sizeof(s_think_buf) - 1)
                s_think_buf[s_think_len++] = c;
            continue;
        case THINK_CLOSING: {
            const char *tag = "/think>";
            if (s_tag_pos < 7 && c == tag[s_tag_pos]) {
                s_tag_pos++;
                if (s_tag_pos == 7) {
                    s_think_buf[s_think_len] = '\0';
                    if (s_reason_cb)
                        s_reason_cb(s_think_buf, s_reason_ud);
                    winalp_log(WINALP_LOG_INFO, "agent: reasoning block (%d chars)", s_think_len);
                    s_think_state = THINK_IDLE;
                }
            } else {
                /* False alarm: the '<' was not </think> */
                /* Re-emit buffered think content + '<' + c */
                s_think_state = THINK_BODY;
                if (s_think_len < (int)sizeof(s_think_buf) - 1)
                    s_think_buf[s_think_len++] = '<';
                /* now c will be handled in THINK_BODY on next iteration */
                /* but we need to re-process c */
                token--; /* un-consume c */
            }
            continue;
        }
        }

        /* Only process JSON when not inside a think block */
        if (s_think_state != THINK_IDLE) continue;

    emit_json:
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
    s_think_state = THINK_IDLE;
    s_tag_pos = 0;
    s_think_len = 0;
}
