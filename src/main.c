#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "../include/winalp.h"
#include "../include/logger.h"
#include "../include/ui_render.h"
#include "../include/stt_engine.h"
#include "../include/audio_capture.h"

/* VAD states for mock speech cycle demo */
enum VadState {
    VAD_IDLE,
    VAD_IN_SPEECH,
    VAD_PROCESSING,
    VAD_RESPONDING
};

/* Callback: STT result → chat panel */
static void on_transcript(const char *text, void *ud) {
    (void)ud;
    if (text && text[0] != '\0') {
        ui_render_push_chat("user", text, "[mic]");
    }
}

int main(void) {
    winalp_log_init("winalp.log");
    winalp_log(WINALP_LOG_INFO, "WinAlp v%d.%d.%d starting...",
               WINALP_VERSION_MAJOR, WINALP_VERSION_MINOR, WINALP_VERSION_PATCH);

    /* Attempt to load STT model — graceful if missing */
    const char *model_path = "models/ggml-tiny.bin";
    if (stt_engine_load(model_path)) {
        winalp_log(WINALP_LOG_INFO, "STT ready");
    } else {
        winalp_log(WINALP_LOG_WARN, "STT model not found at %s — run 'make download-stt-model'", model_path);
    }

    /* Start microphone capture */
    bool mic_ok = audio_capture_start();
    if (!mic_ok)
        winalp_log(WINALP_LOG_WARN, "Mic unavailable — using mock amplitude");

    ui_render_init(1280, 720, "WinAlp AI Assistant");

    char ctx_label[64];
    snprintf(ctx_label, sizeof(ctx_label), "Mic: %s | STT: %s",
             mic_ok ? "live" : "mock",
             stt_engine_is_loaded() ? "ready" : "offline");
    ui_render_set_context_label(ctx_label);

    double mockTime = 0.0;

    AgentState   state     = AGENT_STATE_LISTENING;
    enum VadState vad       = VAD_IDLE;
    double       speechEnd = 0.0;

    while (!ui_render_should_close()) {
        double dt = 1.0 / 60.0;
        mockTime += dt;

        float amplitude;
        if (mic_ok)
            amplitude = audio_capture_rms();
        else
            amplitude = 0.5f + 0.5f * (float)sin(mockTime * 2.5f);

        switch (vad) {

        case VAD_IDLE:
            state = AGENT_STATE_LISTENING;
            if (amplitude > 0.60f) {
                vad       = VAD_IN_SPEECH;
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
                if (n > 0)
                    stt_engine_process(pcm, n, on_transcript, NULL);
            } else {
                on_transcript("(STT offline — model/mic eksik)", NULL);
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
    winalp_log(WINALP_LOG_INFO, "WinAlp shutdown complete");
    winalp_log_shutdown();
    return 0;
}
