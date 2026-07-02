#include "../include/stt_engine.h"
#include "../include/logger.h"

#include "whisper.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static struct whisper_context *s_ctx = NULL;
static CRITICAL_SECTION s_stt_lock;
static int s_lock_inited = 0;

bool stt_engine_is_loaded(void) {
    return s_ctx != NULL;
}

bool stt_engine_load(const char *model_path) {
    if (!s_lock_inited) {
        InitializeCriticalSection(&s_stt_lock);
        s_lock_inited = 1;
    }

    if (s_ctx) {
        whisper_free(s_ctx);
        s_ctx = NULL;
    }

    whisper_context_params cparams = whisper_context_default_params();
    s_ctx = whisper_init_from_file_with_params(model_path, cparams);

    if (!s_ctx) {
        winalp_log(WINALP_LOG_ERROR, "STT: failed to load model: %s", model_path);
        return false;
    }

    winalp_log(WINALP_LOG_INFO, "STT: model loaded: %s", model_path);
    return true;
}

struct stt_cb_data {
    TranscriptCallback cb;
    void *ud;
};

static void stt_log_cb(enum ggml_log_level level, const char *text, void *user_data) {
    (void)level;
    (void)user_data;
    if (text) {
        size_t len = strlen(text);
        if (len > 0 && text[len-1] == '\n') {
            winalp_log(WINALP_LOG_DEBUG, "STT: %.*s", (int)(len-1), text);
        }
    }
}

void stt_engine_process(const float *pcm, int n_samples, TranscriptCallback cb, void *ud) {
    if (!s_ctx) {
        winalp_log(WINALP_LOG_WARN, "STT: engine not loaded, ignoring audio");
        if (cb) cb("", ud);
        return;
    }

    EnterCriticalSection(&s_stt_lock);

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress   = false;
    wparams.print_realtime   = false;
    wparams.print_special    = false;
    wparams.print_timestamps = false;
    wparams.suppress_blank   = true;
    wparams.language         = "tr";
    wparams.n_threads        = 4;
    wparams.offset_ms        = 0;
    wparams.no_context       = true;
    wparams.single_segment   = true;

    whisper_log_set(stt_log_cb, NULL);

    if (whisper_full(s_ctx, wparams, pcm, n_samples) != 0) {
        winalp_log(WINALP_LOG_WARN, "STT: whisper_full failed");
        LeaveCriticalSection(&s_stt_lock);
        if (cb) cb("", ud);
        return;
    }

    int n_segments = whisper_full_n_segments(s_ctx);
    if (n_segments == 0 && cb) {
        LeaveCriticalSection(&s_stt_lock);
        cb("", ud);
        return;
    }

    for (int i = 0; i < n_segments; i++) {
        const char *text = whisper_full_get_segment_text(s_ctx, i);
        if (text && text[0] != '\0' && cb) {
            cb(text, ud);
        }
    }

    LeaveCriticalSection(&s_stt_lock);
}

void stt_engine_unload(void) {
    if (s_ctx) {
        whisper_free(s_ctx);
        s_ctx = NULL;
        winalp_log(WINALP_LOG_INFO, "STT: engine unloaded");
    }
}
