#include "../include/memory_store.h"
#include "../include/dpapi_crypt.h"
#include "../include/logger.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* Encryption header: 4-byte magic identifying DPAPI-encrypted blobs */
static const BYTE ENC_MAGIC[4] = { 0x57, 0x49, 0x4E, 0x45 }; /* "WINE" */
static bool s_encryption_enabled; /* default: off */

#define MAX_PROFILE_KEYS 128
#define MAX_CONV 1024
#define MAX_LINE 4096

static char s_base[1024];
static CRITICAL_SECTION s_lock;
static bool s_init;

/* In-memory profile KV store */
static struct { char key[128]; char val[1024]; } s_profile[MAX_PROFILE_KEYS];
static int s_nprofile;

/* Ring buffer for conversation (also persisted as JSON array) */
static char s_conv[MAX_CONV][MAX_LINE];
static int  s_conv_head, s_conv_count;

/* Forward decl */
static void json_escape_str(const char *in, char *out, size_t out_sz);
static void ensure_dir(const char *dir);
static void load_profile(void);
static void save_profile(void);

bool memory_store_init(const char *base_dir) {
    InitializeCriticalSection(&s_lock);
    strncpy(s_base, base_dir ? base_dir : "profile", sizeof(s_base) - 1);

    ensure_dir(s_base);
    ensure_dir(s_base); /* tasks subdir */
    {
        char tmp[1024]; snprintf(tmp, sizeof(tmp), "%s\\tasks", s_base);
        ensure_dir(tmp);
    }
    {
        char tmp[1024]; snprintf(tmp, sizeof(tmp), "%s\\files", s_base);
        ensure_dir(tmp);
    }
    {
        char tmp[1024]; snprintf(tmp, sizeof(tmp), "%s\\cache", s_base);
        ensure_dir(tmp);
    }

    load_profile();
    s_init = true;
    winalp_log(WINALP_LOG_INFO, "memory_store: initialised at %s", s_base);
    return true;
}

void memory_store_shutdown(void) {
    if (!s_init) return;
    save_profile();
    DeleteCriticalSection(&s_lock);
    s_init = false;
    winalp_log(WINALP_LOG_INFO, "memory_store: shut down");
}

bool memory_store_append_message(const char *role, const char *source,
                                  const char *content) {
    if (!s_init) return false;
    EnterCriticalSection(&s_lock);
    int idx = (s_conv_head + s_conv_count) % MAX_CONV;
    char r_esc[256], s_esc[128], c_esc[MAX_LINE / 2];
    json_escape_str(role ? role : "", r_esc, sizeof(r_esc));
    json_escape_str(source ? source : "", s_esc, sizeof(s_esc));
    json_escape_str(content ? content : "", c_esc, sizeof(c_esc));
    snprintf(s_conv[idx], MAX_LINE, "{\"role\":\"%s\",\"src\":\"%s\",\"msg\":\"%s\"}",
             r_esc, s_esc, c_esc);
    if (s_conv_count < MAX_CONV) s_conv_count++;
    else s_conv_head = (s_conv_head + 1) % MAX_CONV;
    LeaveCriticalSection(&s_lock);
    return true;
}

bool memory_store_set_profile(const char *key, const char *value) {
    if (!key) return false;
    EnterCriticalSection(&s_lock);
    for (int i = 0; i < s_nprofile; i++) {
        if (strcmp(s_profile[i].key, key) == 0) {
            strncpy(s_profile[i].val, value ? value : "", sizeof(s_profile[i].val) - 1);
            save_profile();
            LeaveCriticalSection(&s_lock);
            return true;
        }
    }
    if (s_nprofile < MAX_PROFILE_KEYS) {
        strncpy(s_profile[s_nprofile].key, key, sizeof(s_profile[0].key) - 1);
        strncpy(s_profile[s_nprofile].val, value ? value : "", sizeof(s_profile[0].val) - 1);
        s_nprofile++;
        save_profile();
    }
    LeaveCriticalSection(&s_lock);
    return true;
}

bool memory_store_get_profile(const char *key, char *out, int out_len) {
    if (!key || !out) return false;
    EnterCriticalSection(&s_lock);
    for (int i = 0; i < s_nprofile; i++) {
        if (strcmp(s_profile[i].key, key) == 0) {
            strncpy(out, s_profile[i].val, (size_t)out_len - 1);
            out[out_len - 1] = '\0';
            LeaveCriticalSection(&s_lock);
            return true;
        }
    }
    LeaveCriticalSection(&s_lock);
    if (out_len > 0) out[0] = '\0';
    return false;
}

bool memory_store_upsert_task(const char *task_json) {
    if (!s_init || !task_json) return false;

    /* Extract "id" field from JSON to use as filename */
    char task_id[256] = {0};
    const char *id_key = strstr(task_json, "\"id\"");
    if (id_key) {
        id_key = strchr(id_key, ':');
        if (id_key) {
            id_key++;
            while (*id_key == ' ') id_key++;
            if (*id_key == '"') {
                id_key++;
                int di = 0;
                while (*id_key && *id_key != '"' && di < 255)
                    task_id[di++] = *id_key++;
                task_id[di] = '\0';
            }
        }
    }
    if (!task_id[0])
        snprintf(task_id, sizeof(task_id), "task_%lld", (long long)time(NULL));

    char path[1024]; snprintf(path, sizeof(path), "%s\\tasks\\%s.json", s_base, task_id);
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fputs(task_json, f);
    fclose(f);
    return true;
}

bool memory_store_delete_task(const char *task_id) {
    if (!s_init || !task_id) return false;
    char path[1024]; snprintf(path, sizeof(path), "%s\\tasks\\%s.json", s_base, task_id);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    fclose(f);
    return DeleteFile(path) != 0;
}

bool memory_store_update_task(const char *task_id,
                               const char *field,
                               const char *value) {
    if (!s_init || !task_id || !field) return false;
    char path[1024]; snprintf(path, sizeof(path), "%s\\tasks\\%s.json", s_base, task_id);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    char *buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return false; }
    fread(buf, 1, (size_t)sz, f); buf[sz] = '\0'; fclose(f);

    /* simple field replacement inside JSON */
    char search_key[256]; snprintf(search_key, sizeof(search_key), "\"%s\"", field);
    char *val_start = strstr(buf, search_key);
    if (!val_start) { free(buf); return false; }
    val_start = strchr(val_start, ':');
    if (!val_start) { free(buf); return false; }
    val_start++;
    while (*val_start == ' ') val_start++;
    /* determine current value boundaries */
    char *v_end;
    if (*val_start == '"') {
        val_start++;
        v_end = strchr(val_start, '"');
    } else {
        v_end = strchr(val_start, ',');
        if (!v_end) v_end = strchr(val_start, '}');
    }
    if (!v_end) { free(buf); return false; }
    /* build new JSON */
    size_t prefix_len = (size_t)(val_start - buf);
    size_t suffix_len = (size_t)(sz - (v_end - buf));
    size_t val_len = value ? strlen(value) : 0;
    int quote = (!value || value[0] == '{' || strchr(value, ':') || strchr(value, '}')) ? 0 : 1;
    size_t new_sz = prefix_len + (size_t)(quote ? 1 : 0) +
                    (value ? val_len : 4) + (size_t)(quote ? 1 : 0) +
                    suffix_len + 1;
    char *new_buf = (char*)malloc(new_sz);
    if (!new_buf) { free(buf); return false; }
    char *wp = new_buf;
    memcpy(wp, buf, prefix_len); wp += prefix_len;
    if (quote) *wp++ = '"';
    if (value) { memcpy(wp, value, val_len); wp += val_len; }
    else { memcpy(wp, "null", 4); wp += 4; }
    if (quote) *wp++ = '"';
    memcpy(wp, v_end, suffix_len); wp += suffix_len;
    *wp = '\0';
    free(buf);

    f = fopen(path, "w");
    if (!f) { free(new_buf); return false; }
    fputs(new_buf, f);
    fclose(f);
    free(new_buf);
    return true;
}

bool memory_store_get_tasks(char *out_json, int out_len) {
    if (!s_init || !out_json) return false;
    out_json[0] = '[';
    int pos = 1;
    char search[1024]; snprintf(search, sizeof(search), "%s\\tasks\\*.json", s_base);
    WIN32_FIND_DATA fd; HANDLE h = FindFirstFile(search, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            char path[1024]; snprintf(path, sizeof(path), "%s\\tasks\\%s", s_base, fd.cFileName);
            FILE *f = fopen(path, "r");
            if (f) {
                char buf[4096]; size_t n = fread(buf, 1, sizeof(buf) - 1, f);
                buf[n] = '\0'; fclose(f);
                if (pos > 1 && pos < out_len) out_json[pos++] = ',';
                int rem = out_len - pos;
                int cp = (int)n < rem ? (int)n : rem - 1;
                memcpy(out_json + pos, buf, (size_t)cp);
                pos += cp;
            }
        } while (FindNextFile(h, &fd));
        FindClose(h);
    }
    if (pos < out_len) out_json[pos++] = ']';
    out_json[pos] = '\0';
    return true;
}

/* quick bracket-balance JSON validation */
static bool json_is_valid(const char *buf) {
    int brace = 0, brack = 0, in_str = 0;
    for (const char *p = buf; *p; p++) {
        if (*p == '"' && (p == buf || *(p-1) != '\\')) in_str = !in_str;
        if (in_str) continue;
        if (*p == '{') brace++;
        if (*p == '}') brace--;
        if (*p == '[') brack++;
        if (*p == ']') brack--;
        if (brace < 0 || brack < 0) return false;
    }
    return brace == 0 && brack == 0;
}

static void cache_prune(void) {
    char search[1024]; snprintf(search, sizeof(search), "%s\\cache\\*", s_base);
    WIN32_FIND_DATA fd; HANDLE h = FindFirstFile(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    __int64 now;
    GetSystemTimeAsFileTime((FILETIME*)&now);
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        __int64 ft = ((__int64)fd.ftLastWriteTime.dwHighDateTime << 32)
                   | (__int64)fd.ftLastWriteTime.dwLowDateTime;
        __int64 age_days = (now - ft) / (__int64)10000000 / (__int64)86400;
        if (age_days > 7) {
            char full[1024]; snprintf(full, sizeof(full), "%s\\cache\\%s", s_base, fd.cFileName);
            DeleteFile(full);
        }
    } while (FindNextFile(h, &fd));
    FindClose(h);
}

bool memory_store_integrity_check(void) {
    char path[1024]; snprintf(path, sizeof(path), "%s\\profile.json", s_base);

    /* validate profile.json */
    FILE *f = fopen(path, "r");
    if (f) {
        fseek(f, 0, SEEK_END); long sz = ftell(f);
        if (sz > 0) {
            rewind(f);
            char *buf = (char*)malloc((size_t)sz + 1);
            if (buf) {
                fread(buf, 1, (size_t)sz, f); buf[sz] = '\0';
                if (!json_is_valid(buf)) {
                    winalp_log(WINALP_LOG_WARN, "memory_store: profile.json corrupt — rebuilding");
                    fclose(f);
                    free(buf);
                    save_profile(); /* overwrite with current in-memory state */
                    winalp_log(WINALP_LOG_INFO, "memory_store: profile.json rebuilt");
                    cache_prune();
                    return true;
                }
                free(buf);
            }
        }
        fclose(f);
        winalp_log(WINALP_LOG_INFO, "memory_store: integrity check OK (%s, %ldB)", path, sz);
    }

    /* validate each task file */
    char search[1024]; snprintf(search, sizeof(search), "%s\\tasks\\*.json", s_base);
    WIN32_FIND_DATA fd; HANDLE h = FindFirstFile(search, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            char fp[1024]; snprintf(fp, sizeof(fp), "%s\\tasks\\%s", s_base, fd.cFileName);
            FILE *tf = fopen(fp, "r");
            if (tf) {
                fseek(tf, 0, SEEK_END); long tsz = ftell(tf); rewind(tf);
                if (tsz > 0) {
                    char *tb = (char*)malloc((size_t)tsz + 1);
                    if (tb) {
                        fread(tb, 1, (size_t)tsz, tf); tb[tsz] = '\0';
                        if (!json_is_valid(tb)) {
                            winalp_log(WINALP_LOG_WARN, "memory_store: corrupt task %s — removing", fd.cFileName);
                            fclose(tf); free(tb);
                            DeleteFile(fp);
                            continue;
                        }
                        free(tb);
                    }
                }
                fclose(tf);
            }
        } while (FindNextFile(h, &fd));
        FindClose(h);
    }

    cache_prune();
    return true;
}

/* JSON string escaping: inserts \ before " and \ */
static void json_escape_str(const char *in, char *out, size_t out_sz) {
    if (!in) { if (out_sz > 0) out[0] = '\0'; return; }
    size_t oi = 0;
    for (const char *p = in; *p && oi + 6 < out_sz; p++) {
        if      (*p == '"')  { out[oi++] = '\\'; out[oi++] = '"'; }
        else if (*p == '\\') { out[oi++] = '\\'; out[oi++] = '\\'; }
        else if (*p == '\n') { out[oi++] = '\\'; out[oi++] = 'n'; }
        else if (*p == '\r') { out[oi++] = '\\'; out[oi++] = 'r'; }
        else if (*p == '\t') { out[oi++] = '\\'; out[oi++] = 't'; }
        else                  out[oi++] = *p;
    }
    out[oi] = '\0';
}

/* Build a human-readable profile summary string from all stored KV pairs */
bool memory_store_build_summary(char *out, int out_len) {
    if (!out || out_len <= 0) return false;
    EnterCriticalSection(&s_lock);
    int pos = 0;
    pos += snprintf(out + pos, (size_t)(out_len - pos), "Kullanici hakkinda bilinenler:\n");
    for (int i = 0; i < s_nprofile && pos < out_len - 2; i++) {
        char val_esc[1024];
        json_escape_str(s_profile[i].val, val_esc, sizeof(val_esc));
        pos += snprintf(out + pos, (size_t)(out_len - pos), "- %s: %s\n",
                        s_profile[i].key, val_esc);
    }
    LeaveCriticalSection(&s_lock);
    return pos > 0;
}

/* ---------- internal helpers ---------- */

static void ensure_dir(const char *dir) {
    if (!CreateDirectory(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        winalp_log(WINALP_LOG_WARN, "memory_store: cannot create %s", dir);
}

static int read_file_raw(const char *path, char **out_buf) {
    *out_buf = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    char *buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    *out_buf = buf;
    return (int)n;
}

static void load_profile(void) {
    char path[1024]; snprintf(path, sizeof(path), "%s\\profile.json", s_base);
    char *raw = NULL;
    int nread = read_file_raw(path, &raw);
    if (!raw || nread <= 0) { free(raw); return; }

    char *json_buf = NULL;
    if (nread >= 4 && memcmp(raw, ENC_MAGIC, 4) == 0) {
        /* Encrypted — decrypt */
        BYTE *blob = (BYTE*)(raw + 4);
        DWORD blob_len = (DWORD)(nread - 4);
        DWORD dec_len = 0;
        char *dec = NULL;
        if (dpapi_decrypt(blob, blob_len, &dec, &dec_len)) {
            json_buf = (char*)malloc((size_t)dec_len + 1);
            if (json_buf) {
                memcpy(json_buf, dec, dec_len);
                json_buf[dec_len] = '\0';
            }
            dpapi_free((BYTE*)dec);
        }
        free(raw);
    } else {
        json_buf = raw; /* plain text */
    }

    if (!json_buf) return;

    /* minimal JSON object parser: find "key":"value" pairs */
    char *p = json_buf;
    while ((p = strstr(p, "\"")) && s_nprofile < MAX_PROFILE_KEYS) {
        p++;
        int ki = 0;
        while (*p && *p != '"' && ki < (int)sizeof(s_profile[s_nprofile].key) - 1)
            s_profile[s_nprofile].key[ki++] = *p++;
        s_profile[s_nprofile].key[ki] = '\0';
        if (*p) p++;
        while (*p && (*p == ':' || *p == ' ')) p++;
        if (*p == '"') {
            p++;
            int vi = 0;
            while (*p && *p != '"' && vi < (int)sizeof(s_profile[s_nprofile].val) - 1)
                s_profile[s_nprofile].val[vi++] = *p++;
            s_profile[s_nprofile].val[vi] = '\0';
            if (*p) p++;
            s_nprofile++;
        }
    }
    free(json_buf);
}

static void save_profile(void) {
    char path[1024]; snprintf(path, sizeof(path), "%s\\profile.json", s_base);

    /* Build JSON in memory buffer */
    char buf[65536];
    int pos = 0;
    int rem = sizeof(buf);

#define APPEND(fmt, ...) do { \
    int n = snprintf(buf + pos, (size_t)rem, fmt, ##__VA_ARGS__); \
    if (n < 0 || n >= rem) { pos = 0; rem = 0; } \
    else { pos += n; rem -= n; } \
} while(0)

    APPEND("{\n");
    for (int i = 0; i < s_nprofile; i++) {
        char val_esc[2048];
        json_escape_str(s_profile[i].val, val_esc, sizeof(val_esc));
        APPEND("  \"%s\": \"%s\"%s\n",
               s_profile[i].key, val_esc,
               (i < s_nprofile - 1) ? "," : "");
    }
    APPEND("}\n");
    if (pos == 0) return;

    if (s_encryption_enabled) {
        /* Encrypt with DPAPI */
        BYTE *blob = NULL;
        DWORD blob_len = 0;
        if (!dpapi_encrypt(buf, (size_t)pos, &blob, &blob_len)) {
            winalp_log(WINALP_LOG_WARN, "memory_store: DPAPI encrypt failed");
            return;
        }
        /* Write magic header + encrypted blob */
        FILE *f = fopen(path, "wb");
        if (!f) { dpapi_free(blob); return; }
        fwrite(ENC_MAGIC, 1, 4, f);
        fwrite(blob, 1, (size_t)blob_len, f);
        fclose(f);
        dpapi_free(blob);
    } else {
        /* Write plaintext */
        FILE *f = fopen(path, "w");
        if (!f) return;
        fputs(buf, f);
        fclose(f);
    }

#undef APPEND
}

void memory_store_set_encryption(bool enable) {
    s_encryption_enabled = enable;
    if (s_init) save_profile();
    winalp_log(WINALP_LOG_INFO, "memory_store: encryption %s",
               enable ? "enabled" : "disabled");
}
