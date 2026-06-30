#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "../include/winalp.h"
#include "../include/logger.h"
#include "../include/ui_render.h"
#include "../include/stt_engine.h"
#include "../include/audio_capture.h"
#include "../include/ai_engine.h"
#include "../include/system_agent.h"
#include "../include/memory_store.h"
#include "../include/context_tracker.h"
#include "../include/prompt_engine.h"
#include "../include/plugin_manager.h"

enum VadState { VAD_IDLE, VAD_IN_SPEECH, VAD_PROCESSING, VAD_RESPONDING };

static char s_ai_buf[8192];
static int  s_ai_len;

static bool confirm_cb(const char *desc, void *ud) {
    (void)ud;
    winalp_log(WINALP_LOG_INFO, "agent: confirming: %s", desc);
    return true;
}

static void on_ai_token(const char *token, void *ud) {
    (void)ud;
    system_agent_feed(token);
    int n = (int)strlen(token);
    if (s_ai_len + n < (int)sizeof(s_ai_buf) - 1) {
        memcpy(s_ai_buf + s_ai_len, token, (size_t)n);
        s_ai_len += n;
        s_ai_buf[s_ai_len] = '\0';
    }
}

static void on_transcript(const char *text, void *ud) {
    (void)ud;
    if (!text || text[0] == '\0') return;
    ui_render_push_chat("user", text, "[mic]");
    memory_store_append_message("user", "mic", text);

    if (ai_engine_is_loaded()) {
        s_ai_len = 0;
        s_ai_buf[0] = '\0';
        ai_engine_infer(text, on_ai_token, NULL);
        system_agent_flush();
        if (s_ai_len > 0) {
            ui_render_push_chat("assistant", s_ai_buf, "[AI]");
            memory_store_append_message("assistant", "ai", s_ai_buf);
        }
    }
}

int main(void) {
    winalp_log_init("winalp.log");
    srand((unsigned)time(NULL));
    winalp_log(WINALP_LOG_INFO, "WinAlp v%d.%d.%d starting...",
               WINALP_VERSION_MAJOR, WINALP_VERSION_MINOR, WINALP_VERSION_PATCH);

    /* Initialise persistent storage */
    memory_store_init("profile");
    memory_store_integrity_check();

    /* STT */
    const char *stt_path = "models/ggml-tiny.bin";
    if (stt_engine_load(stt_path))
        winalp_log(WINALP_LOG_INFO, "STT ready");
    else
        winalp_log(WINALP_LOG_WARN, "STT model not found: %s", stt_path);

    /* AI engine */
    const char *llm_path = "models/brain-model.gguf";
    bool ai_ok = ai_engine_load(llm_path, 0);
    if (ai_ok)
        winalp_log(WINALP_LOG_INFO, "AI engine ready");
    else
        winalp_log(WINALP_LOG_WARN, "AI model not found: %s", llm_path);

    /* Prompt engine + plugins */
    prompt_engine_init("prompts");
    plugin_manager_init("plugins");
    plugin_manager_scan();

    /* System agent */
    system_agent_set_confirm_cb(confirm_cb, NULL);

    /* Mic */
    bool mic_ok = audio_capture_start();
    if (!mic_ok)
        winalp_log(WINALP_LOG_WARN, "Mic unavailable — mock amplitude");

    ui_render_init(1280, 720, "WinAlp AI Assistant");

    double mockTime = 0.0;
    AgentState state = AGENT_STATE_LISTENING;
    enum VadState vad = VAD_IDLE;
    double speechEnd = 0.0;

    while (!ui_render_should_close()) {
        double dt = 1.0 / 60.0;
        mockTime += dt;

        float amplitude;
        if (mic_ok)
            amplitude = audio_capture_rms();
        else
            amplitude = 0.5f + 0.5f * (float)sin(mockTime * 2.5f);

        /* Update context label every 30 frames */
        static int ctx_counter = 0;
        if (++ctx_counter % 30 == 0) {
            char ctx[256];
            context_tracker_poll(ctx, sizeof(ctx));
            ui_render_set_context_label(ctx);
        }

        switch (vad) {
        case VAD_IDLE:
            state = AGENT_STATE_LISTENING;
            if (amplitude > 0.60f) {
                vad = VAD_IN_SPEECH;
                speechEnd = mockTime + 1.5;
            }
            break;
        case VAD_IN_SPEECH:
            state = AGENT_STATE_LISTENING;
            if (mockTime >= speechEnd)
                vad = VAD_PROCESSING;
            break;
        case VAD_PROCESSING:
            state = AGENT_STATE_THINKING;
            if (stt_engine_is_loaded() && mic_ok) {
                float pcm[16000 * 2];
                int n = audio_capture_read(pcm, 16000 * 2);
                if (n > 0) {
                    s_ai_len = 0;
                    s_ai_buf[0] = '\0';
                    stt_engine_process(pcm, n, on_transcript, NULL);
                }
            } else {
                on_transcript("(STT offline)", NULL);
            }
            vad = VAD_RESPONDING;
            break;
        case VAD_RESPONDING:
            state = AGENT_STATE_WRITING;
            if (amplitude < 0.40f)
                vad = VAD_IDLE;
            break;
        }

        ui_render_frame(state, amplitude);
    }

    ui_render_shutdown();
    audio_capture_stop();
    stt_engine_unload();
    ai_engine_unload();
    prompt_engine_shutdown();
    plugin_manager_shutdown();
    memory_store_shutdown();
    winalp_log(WINALP_LOG_INFO, "WinAlp shutdown complete");
    winalp_log_shutdown();
    return 0;
}
