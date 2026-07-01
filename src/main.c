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
#include "../include/thread_pool.h"
#include "../include/vision_engine.h"

static bool confirm_cb(const char *desc, void *ud) {
    (void)ud;
    winalp_log(WINALP_LOG_INFO, "agent: confirm: %s", desc);
    return ui_render_confirm_blocking("WinAlp — Confirm Action", desc);
}

int main(void) {
    winalp_log_init("winalp.log");
    srand((unsigned)time(NULL));
    winalp_log(WINALP_LOG_INFO, "WinAlp v%d.%d.%d starting...",
               WINALP_VERSION_MAJOR, WINALP_VERSION_MINOR, WINALP_VERSION_PATCH);

    memory_store_init("profile");
    memory_store_integrity_check();

    /* STT */
    const char *stt_path = "models/ggml-tiny.bin";
    if (stt_engine_load(stt_path))
        winalp_log(WINALP_LOG_INFO, "STT ready");
    else
        winalp_log(WINALP_LOG_WARN, "STT model not found: %s", stt_path);

    /* Model selection */
    ModelEntry models[64]; int n_models = 0;
    WIN32_FIND_DATA fd;
    HANDLE hf = FindFirstFileA("models\\*.gguf", &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do {
            if (n_models >= 64) break;
            strncpy(models[n_models].label, fd.cFileName, sizeof(models[n_models].label) - 1);
            snprintf(models[n_models].path, sizeof(models[n_models].path), "models\\%s", fd.cFileName);
            ULARGE_INTEGER sz;
            sz.LowPart  = fd.nFileSizeLow;
            sz.HighPart = fd.nFileSizeHigh;
            models[n_models].size_mb = sz.QuadPart / (1024 * 1024);
            models[n_models].tier = models[n_models].size_mb > 8000 ? 2 : 1;
            n_models++;
        } while (FindNextFileA(hf, &fd));
        FindClose(hf);
    }

    char selected_model[1024] = "";
    if (n_models == 1) {
        strncpy(selected_model, models[0].path, sizeof(selected_model) - 1);
        winalp_log(WINALP_LOG_INFO, "model: auto-selected %s", selected_model);
    } else if (n_models > 1) {
        char *sel = ui_render_model_select(models, n_models);
        if (sel) {
            strncpy(selected_model, sel, sizeof(selected_model) - 1);
            free(sel);
        }
    }

    bool ai_ok = false;
    if (selected_model[0]) {
        ai_ok = ai_engine_load_auto(selected_model);
        if (ai_ok)
            winalp_log(WINALP_LOG_INFO, "AI engine ready: %s", selected_model);
        else
            winalp_log(WINALP_LOG_WARN, "AI engine failed: %s", selected_model);
    } else {
        winalp_log(WINALP_LOG_WARN, "No GGUF model found in models/");
    }

    prompt_engine_init("prompts");
    plugin_manager_init("plugins");
    plugin_manager_scan();

    system_agent_set_confirm_cb(confirm_cb, NULL);
    system_agent_set_plugin_action_cb(plugin_manager_execute_action, NULL);

    bool mic_ok = audio_capture_start();
    if (!mic_ok)
        winalp_log(WINALP_LOG_WARN, "Mic unavailable");

    /* Start background threads */
    vision_engine_init();
    thread_pool_start_all();

    ui_render_init(1280, 720, "WinAlp AI Assistant");

    bool waiting_for_ai = false;
    char vision_buf[4096] = {0};
    int ctx_counter = 0;

    while (!ui_render_should_close()) {
        float amplitude = mic_ok ? audio_capture_rms() : 0.5f;

        /* Poll for STT transcripts from the background STT thread */
        char transcript[4096];
        if (thread_pool_get_transcript(transcript, sizeof(transcript))) {
            winalp_log(WINALP_LOG_INFO, "main: transcript received (%d chars)", (int)strlen(transcript));
            ui_render_push_chat("user", transcript, "[mic]");
            memory_store_append_message("user", "mic", transcript);
            waiting_for_ai = true;
        }

        /* Poll for AI responses */
        char response[8192];
        if (thread_pool_get_ai_response(response, sizeof(response))) {
            winalp_log(WINALP_LOG_INFO, "main: AI response (%d chars)", (int)strlen(response));
            /* Feed response through system_agent for JSON action parsing */
            {
                const char *p = response;
                while (*p) {
                    char ch[2] = { *p++, 0 };
                    system_agent_feed(ch);
                }
                system_agent_flush();
            }
            ui_render_push_chat("assistant", response, "[AI]");
            memory_store_append_message("assistant", "ai", response);
            waiting_for_ai = false;
        }

        /* Poll for vision OCR results */
        char vision[4096];
        if (thread_pool_get_vision_text(vision, sizeof(vision))) {
            strncpy(vision_buf, vision, sizeof(vision_buf) - 1);
            winalp_log(WINALP_LOG_INFO, "main: vision: %s", vision);
        }

        /* Context tracker */
        if (++ctx_counter % 30 == 0) {
            char ctx[256];
            context_tracker_poll(ctx, sizeof(ctx));
            ui_render_set_context_label(ctx);
        }

        /* Agent state for orb animation */
        AgentState state = waiting_for_ai ? AGENT_STATE_THINKING : AGENT_STATE_LISTENING;

        ui_render_frame(state, amplitude);
    }

    thread_pool_stop_all();
    vision_engine_shutdown();
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
