#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <signal.h>

#include "../include/winalp.h"
#include "../include/logger.h"
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
#include "../include/lua_runtime.h"
#include "../include/thread_pool.h"
#include "../include/vision_engine.h"
#include "../include/sys_diag.h"
#include "../include/doc_router.h"
#include "../include/tts_engine.h"

#define MAX_PCM_SAMPLES (16000 * 15) /* 15 seconds at 16kHz */
static float s_pcm_buf[MAX_PCM_SAMPLES];
static int s_pcm_count = 0;

static void on_stt_result(const char *text, void *ud);

struct SttJob { float *buf; int count; };

static DWORD WINAPI stt_thread_proc(LPVOID arg) {
    struct SttJob *job = (struct SttJob*)arg;
    if (job && job->buf && job->count > 0)
        stt_engine_process(job->buf, job->count, on_stt_result, NULL);
    if (job) { free(job->buf); free(job); }
    return 0;
}

static void on_stt_result(const char *text, void *ud) {
    (void)ud;
    if (!text || !text[0]) return;
    winalp_log(WINALP_LOG_INFO, "main: push-to-talk: %s", text);
    ui_render_set_transcript(text);
    memory_store_append_message("user", "mic", text);
    thread_pool_send_text(text);
}

static bool confirm_cb(const char *desc, void *ud) {
    (void)ud;
    winalp_log(WINALP_LOG_INFO, "agent: confirm: %s", desc);
    return ui_render_confirm_blocking("WinAlp — Confirm Action", desc);
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep) {
    (void)ep;
    winalp_log(WINALP_LOG_ERROR, "CRASH: unhandled exception — continuing");
    return EXCEPTION_EXECUTE_HANDLER;
}
static void sig_handler(int sig) {
    winalp_log(WINALP_LOG_ERROR, "CRASH: signal %d — continuing", sig);
}

int main(void) {
    winalp_log_init("winalp.log");
    srand((unsigned)time(NULL));
    winalp_log(WINALP_LOG_INFO, "WinAlp v%d.%d.%d starting...",
               WINALP_VERSION_MAJOR, WINALP_VERSION_MINOR, WINALP_VERSION_PATCH);

    /* Install crash handlers to prevent unexpected exit */
    SetUnhandledExceptionFilter(crash_filter);
    signal(SIGSEGV, sig_handler);
    signal(SIGABRT, sig_handler);
    signal(SIGFPE,  sig_handler);

    memory_store_init("profile");
    memory_store_integrity_check();

    /* Auto-create required directories */
    CreateDirectoryA("prompts", NULL);
    CreateDirectoryA("models", NULL);
    CreateDirectoryA("scripts", NULL);
    CreateDirectoryA("work", NULL);

    /* STT */
    const char *stt_path = "models/ggml-tiny.bin";
    if (stt_engine_load(stt_path))
        winalp_log(WINALP_LOG_INFO, "STT ready");
    else
        winalp_log(WINALP_LOG_WARN, "STT model not found: %s", stt_path);

    /* Model selection */
    ModelEntry models[64]; int n_models = 0;

    /* Detect system RAM/VRAM for compatibility checking */
    SysDiag sysdiag;
    sys_diag_detect(&sysdiag);

    /* Scan for companion mmproj-*.gguf (VLM projector) files */
    bool vlm_available = false;
    WIN32_FIND_DATA mmfd;
    HANDLE hm = FindFirstFileA("models\\mmproj-*.gguf", &mmfd);
    if (hm != INVALID_HANDLE_VALUE) { vlm_available = true; FindClose(hm); }

    WIN32_FIND_DATA fd;
    HANDLE hf = FindFirstFileA("models\\*.gguf", &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do {
            if (n_models >= 64) break;
            /* Skip mmproj files */
            if (strstr(fd.cFileName, "mmproj-") != NULL) continue;

            ModelEntry *m = &models[n_models];
            strncpy(m->label, fd.cFileName, sizeof(m->label) - 1);
            snprintf(m->path, sizeof(m->path), "models\\%s", fd.cFileName);
            ULARGE_INTEGER sz;
            sz.LowPart  = fd.nFileSizeLow;
            sz.HighPart = fd.nFileSizeHigh;
            m->size_mb = sz.QuadPart / (1024 * 1024);

            /* Detect architecture from filename (heuristic — fast, no model load) */
            const char *name = fd.cFileName;
            /* Lowercase copy for matching */
            char lname[256]; int li = 0;
            for (const char *p = name; *p && li < 255; p++) lname[li++] = (char)tolower((unsigned char)*p);
            lname[li] = '\0';

            if      (strstr(lname, "qwen"))   strncpy(m->arch, "qwen", sizeof(m->arch) - 1);
            else if (strstr(lname, "llama"))  strncpy(m->arch, "llama", sizeof(m->arch) - 1);
            else if (strstr(lname, "deepseek")) strncpy(m->arch, "deepseek", sizeof(m->arch) - 1);
            else if (strstr(lname, "llava") || strstr(lname, "bakllava"))
                                              strncpy(m->arch, "llava", sizeof(m->arch) - 1);
            else if (strstr(lname, "minicpm") || strstr(lname, "minicpmv"))
                                              strncpy(m->arch, "minicpm", sizeof(m->arch) - 1);
            else                              strncpy(m->arch, "unknown", sizeof(m->arch) - 1);

            /* Check if this is a VLM-capable model (needs companion mmproj) */
            m->flags = 0;
            if (vlm_available) {
                /* VLM models usually have "vl" or "vision" in the name */
                if (strstr(lname, "vl") || strstr(lname, "vision") ||
                    strcmp(m->arch, "llava") == 0 || strcmp(m->arch, "minicpm") == 0)
                    m->flags |= 1;
            }

            /* Tier based on size */
                 if (m->size_mb < 1500) m->tier = 0; /* hafif */
            else if (m->size_mb < 8000) m->tier = 1; /* orta */
            else                         m->tier = 2; /* guclu */

            /* Compatibility: estimate needed RAM ≈ file size + 1GB overhead for context */
            unsigned long long needed_mb = m->size_mb + 1024;
            unsigned long long free_mb = sysdiag.ram_free_mb + sysdiag.vram_free_mb;
                 if (free_mb >= needed_mb + 1024) m->compat = 3; /* green */
            else if (free_mb >= needed_mb)         m->compat = 2; /* yellow */
            else                                    m->compat = 1; /* red */

            n_models++;
        } while (FindNextFileA(hf, &fd));
        FindClose(hf);
    }

    char selected_model[1024] = "";
    if (n_models == 1 && models[0].compat > 1) {
        strncpy(selected_model, models[0].path, sizeof(selected_model) - 1);
        winalp_log(WINALP_LOG_INFO, "model: auto-selected %s", selected_model);
    } else if (n_models > 0) {
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

    /* Load UI theme from Lua if available */
    {
        lua_State *L_theme = lua_runtime_new_state("scripts");
        if (L_theme && lua_runtime_dofile(L_theme, "scripts/ui_theme.lua")) {
            const char *speed_str = lua_runtime_dostring_result(L_theme, "return theme.orb_speed");
            const char *scale_str = lua_runtime_dostring_result(L_theme, "return theme.orb_scale");
            const char *maxp_str  = lua_runtime_dostring_result(L_theme, "return theme.max_particles");
            if (speed_str) ui_render_set_theme_float("orb_speed", (float)atof(speed_str));
            if (scale_str) ui_render_set_theme_float("orb_scale", (float)atof(scale_str));
            if (maxp_str)  ui_render_set_theme_float("max_particles", (float)atof(maxp_str));
            winalp_log(WINALP_LOG_INFO, "theme: loaded scripts/ui_theme.lua");
        }
        if (L_theme) lua_runtime_close(L_theme);
    }

    /* Initialise document router (PDF/OCR/VLM) */
    doc_router_init("models");

    /* Start background threads */
    vision_engine_init();
    thread_pool_start_all();

    tts_engine_init();
    ui_render_init(1280, 720, "WinAlp AI Assistant");

    bool waiting_for_ai = false;
    char vision_buf[4096] = {0};
    int ctx_counter = 0;

    bool was_octagon_held = false;

    while (!ui_render_should_close()) {
        float amplitude = mic_ok ? audio_capture_rms() : 0.5f;

        /* Push-to-talk via octagon hold */
        {
            bool held = ui_render_is_octagon_held();
            if (held && !was_octagon_held) {
                s_pcm_count = 0;
                audio_capture_set_exclusive(true);
                if (stt_engine_is_loaded())
                    winalp_log(WINALP_LOG_INFO, "main: push-to-talk started");
                else
                    winalp_log(WINALP_LOG_WARN, "main: STT not loaded - cannot record");
            }
            if (held && stt_engine_is_loaded()) {
                int n = audio_capture_read(s_pcm_buf + s_pcm_count,
                                           MAX_PCM_SAMPLES - s_pcm_count);
                if (n > 0) s_pcm_count += n;
            }
            if (!held && was_octagon_held) {
                audio_capture_set_exclusive(false);
                if (s_pcm_count > 64 && stt_engine_is_loaded()) {
                    float *copy = (float*)malloc(s_pcm_count * sizeof(float));
                    if (copy) {
                        memcpy(copy, s_pcm_buf, s_pcm_count * sizeof(float));
                        struct SttJob *job = (struct SttJob*)malloc(sizeof(struct SttJob));
                        if (job) {
                            job->buf = copy;
                            job->count = s_pcm_count;
                            HANDLE h = CreateThread(NULL, 0, stt_thread_proc, job, 0, NULL);
                            if (h) CloseHandle(h); else { free(job->buf); free(job); }
                        } else {
                            free(copy);
                        }
                    }
                }
            }
            was_octagon_held = held;
        }

        /* Poll for keyboard text input — send to background AI thread */
        {
            char kb_input[4096];
            if (ui_render_has_text_input()) {
                ui_render_get_text_input(kb_input, sizeof(kb_input));
                if (kb_input[0]) {
                    winalp_log(WINALP_LOG_INFO, "main: keyboard input (%d chars)", (int)strlen(kb_input));
                    ui_render_push_chat("user", kb_input, "[key]");
                    memory_store_append_message("user", "key", kb_input);
                    waiting_for_ai = true;
                    thread_pool_send_text(kb_input);
                }
            }
        }

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
            tts_engine_speak_async(response);
            waiting_for_ai = false;
        }

        /* Poll for vision OCR results */
        char vision[4096];
        if (thread_pool_get_vision_text(vision, sizeof(vision))) {
            strncpy(vision_buf, vision, sizeof(vision_buf) - 1);
            winalp_log(WINALP_LOG_INFO, "main: vision: %s", vision);
        }

        /* Context tracker + profile + task strip (throttled) */
        if (++ctx_counter % 30 == 0) {
            char ctx[256];
            context_tracker_poll(ctx, sizeof(ctx));

            /* Append vision context if available */
            if (vision_buf[0]) {
                size_t vlen = strlen(vision_buf);
                size_t clen = strlen(ctx);
                if (clen + vlen + 4 < sizeof(ctx)) {
                    snprintf(ctx + clen, sizeof(ctx) - clen, " | OCR: %s", vision_buf);
                }
            }
            ui_render_set_context_label(ctx);

            /* Feed dynamic context to AI engine so it knows the user's active environment */
            {
                char dyn_ctx[2048];
                snprintf(dyn_ctx, sizeof(dyn_ctx), "Active Window: %s", ctx);
                if (vision_buf[0]) {
                    size_t cur = strlen(dyn_ctx);
                    snprintf(dyn_ctx + cur, sizeof(dyn_ctx) - cur,
                             "\nScreen OCR: %s", vision_buf);
                }
                ai_engine_set_dynamic_context(dyn_ctx);
            }
        }

        /* Profile label every 120 frames */
        if (ctx_counter % 120 == 0) {
            char name[256] = "";
            if (memory_store_get_profile("name", name, sizeof(name)))
                ui_render_set_profile_label(name);
        }

        /* Task strip every 120 frames */
        if (ctx_counter % 120 == 0) {
            char tasks[4096];
            if (memory_store_get_tasks(tasks, sizeof(tasks)) && tasks[0] != '[')
                ui_render_set_task_strip(tasks);
        }

        /* Agent state for orb animation */
        AgentState state = waiting_for_ai ? AGENT_STATE_THINKING : AGENT_STATE_LISTENING;

        ui_render_frame(state, amplitude);
    }

    tts_engine_stop();
    thread_pool_stop_all();
    vision_engine_shutdown();
    ui_render_shutdown();
    tts_engine_shutdown();
    audio_capture_stop();
    stt_engine_unload();
    ai_engine_unload();
    doc_router_shutdown();
    prompt_engine_shutdown();
    plugin_manager_shutdown();
    memory_store_shutdown();
    winalp_log(WINALP_LOG_INFO, "WinAlp shutdown complete");
    winalp_log_shutdown();
    return 0;
}
