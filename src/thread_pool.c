#include "../include/thread_pool.h"
#include "../include/logger.h"
#include "../include/audio_capture.h"
#include "../include/stt_engine.h"
#include "../include/ai_engine.h"
#include "../include/vision_engine.h"
#include "../include/memory_store.h"
#include "../include/plugin_manager.h"
#include "../include/prompt_engine.h"
#include "../include/system_agent.h"
#include "../include/thread_mutex.h"
#include "../include/ui_render.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <stdlib.h>

/* ---------- thread-safe message queues ---------- */
#define QUEUE_SIZE 16
#define MSG_SIZE 4096

typedef struct {
    char buf[QUEUE_SIZE][MSG_SIZE];
    int head, count;
    ThreadMutex mtx;
    HANDLE signal; /* auto-reset event */
} MsgQueue;

static void queue_init(MsgQueue *q) {
    thread_mutex_init(&q->mtx);
    q->signal = CreateEventA(NULL, FALSE, FALSE, NULL);
    q->head = q->count = 0;
}

static void queue_push(MsgQueue *q, const char *msg) {
    thread_mutex_lock(&q->mtx);
    if (q->count < QUEUE_SIZE) {
        int idx = (q->head + q->count) % QUEUE_SIZE;
        strncpy(q->buf[idx], msg ? msg : "", MSG_SIZE - 1);
        q->count++;
        SetEvent(q->signal);
    }
    thread_mutex_unlock(&q->mtx);
}

static bool queue_pop(MsgQueue *q, char *out, int out_len) {
    thread_mutex_lock(&q->mtx);
    if (q->count == 0) { thread_mutex_unlock(&q->mtx); return false; }
    strncpy(out, q->buf[q->head], (size_t)out_len - 1);
    out[out_len - 1] = '\0';
    q->head = (q->head + 1) % QUEUE_SIZE;
    q->count--;
    thread_mutex_unlock(&q->mtx);
    return true;
}

/* ---------- thread handles + control ---------- */
static HANDLE s_threads[4];
static int s_nthreads;
static volatile bool s_running;

/* Queue instances */
static MsgQueue s_transcript_q;
static MsgQueue s_ai_response_q;
static MsgQueue s_vision_q;

/* ---------- STT thread ---------- */
/* Called by stt_engine_process on STT thread — shows chat + pushes to AI queue */
static void on_stt_result(const char *text, void *ud) {
    (void)ud;
    if (text && text[0]) {
        ui_render_set_transcript(text);
        ui_render_push_chat("user", text, "[mic]");
        memory_store_append_message("user", "mic", text);
        queue_push(&s_transcript_q, text);
    }
}

/* VAD state machine */
#define VAD_THRESHOLD_ON  0.45f   /* speech start threshold */
#define VAD_THRESHOLD_OFF 0.25f   /* speech end threshold (hysteresis) */
#define VAD_SILENCE_MS    800     /* ms of silence before declaring speech ended */
#define VAD_MIN_SPEECH_MS 300     /* minimum speech duration to trigger STT */
#define VAD_MAX_SPEECH_MS 8000    /* max speech capture before forced STT */
#define VAD_SAMPLE_RATE   16000

/* Ring buffer for accumulating speech samples */
#define VAD_BUF_MAX (VAD_SAMPLE_RATE * 8) /* 8s max */
static float s_vad_buf[VAD_BUF_MAX];
static int s_vad_count;

static DWORD WINAPI stt_thread(LPVOID arg) {
    (void)arg;
    winalp_log(WINALP_LOG_INFO, "thread_pool: STT thread started");

    int vad_state = 0; /* 0=silence, 1=speech_candidate, 2=speaking */
    int silence_ms = 0;
    int speech_ms = 0;

    while (s_running) {
        if (!audio_capture_is_initialised()) {
            Sleep(50);
            continue;
        }

        float amplitude = audio_capture_rms();

        switch (vad_state) {
            case 0: /* Silence — waiting for speech */
                if (amplitude > VAD_THRESHOLD_ON) {
                    vad_state = 1;
                    speech_ms = 0;
                }
                Sleep(50);
                break;

            case 1: /* Speech candidate — confirm sustained speech */
                speech_ms += 50;
                if (amplitude > VAD_THRESHOLD_ON) {
                    if (speech_ms >= VAD_MIN_SPEECH_MS) {
                        vad_state = 2;
                        silence_ms = 0;
                        s_vad_count = 0;
                        winalp_log(WINALP_LOG_INFO, "thread_pool: VAD: speech started");
                    }
                } else {
                    vad_state = 0;
                }
                Sleep(50);
                break;

            case 2: /* Speaking — read audio incrementally */
                {
                    int n = audio_capture_read(s_vad_buf + s_vad_count,
                                               VAD_BUF_MAX - s_vad_count);
                    if (n > 0) s_vad_count += n;
                }

                speech_ms += 50;
                if (amplitude > VAD_THRESHOLD_OFF) {
                    silence_ms = 0;
                } else {
                    silence_ms += 50;
                }

                if (silence_ms >= VAD_SILENCE_MS || speech_ms >= VAD_MAX_SPEECH_MS ||
                    s_vad_count >= VAD_BUF_MAX) {
                    if (!s_running) break;
                    winalp_log(WINALP_LOG_INFO,
                               "thread_pool: VAD: speech ended (%d ms, %d samples captured)",
                               speech_ms, s_vad_count);

                    if (s_vad_count >= 1600 && stt_engine_is_loaded()) {
                        winalp_log(WINALP_LOG_INFO,
                                   "thread_pool: STT processing %d samples", s_vad_count);
                        stt_engine_process(s_vad_buf, s_vad_count, on_stt_result, NULL);
                    }

                    vad_state = 0;
                    speech_ms = 0;
                    silence_ms = 0;
                    s_vad_count = 0;
                    continue;
                }
                Sleep(30); /* read more frequently during speech */
                break;
        }
    }
    winalp_log(WINALP_LOG_INFO, "thread_pool: STT thread stopped");
    return 0;
}

/* ---------- AI thread ---------- */
/* Crash recovery via Vectored Exception Handler (works on all Windows) */
static jmp_buf s_ai_jmp;
static DWORD s_ai_thread_id = 0;

static LONG CALLBACK ai_veh(EXCEPTION_POINTERS *ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        GetCurrentThreadId() == s_ai_thread_id) {
        winalp_log(WINALP_LOG_ERROR, "AI: access violation caught by VEH, recovering...");
        longjmp(s_ai_jmp, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* Accumulator passed as userdata to on_ai_token */
typedef struct {
    char buf[4096];
    int  len;
} AccumState;

static void on_ai_token(const char *token, void *ud) {
    AccumState *as = (AccumState*)ud;
    if (!as || !token) return;
    int n = (int)strlen(token);
    if (as->len + n < (int)sizeof(as->buf) - 1) {
        memcpy(as->buf + as->len, token, (size_t)n);
        as->len += n;
        as->buf[as->len] = '\0';
    }
}

/* Build and set the system prompt (profile + plugins + template) */
static void rebuild_system_prompt(void) {
    char profile_summary[2048] = "";
    memory_store_build_summary(profile_summary, sizeof(profile_summary));

    char *plugin_guide = plugin_manager_build_guide();

    char *sys = prompt_engine_build(
        profile_summary[0] ? profile_summary : NULL,
        plugin_guide && plugin_guide[0] ? plugin_guide : NULL);
    if (sys) {
        ai_engine_set_system_prompt(sys);
        free(sys);
    } else {
        ai_engine_set_system_prompt("You are WinAlp, a desktop AI assistant.");
    }
    free(plugin_guide);
}

static DWORD WINAPI ai_thread(LPVOID arg) {
    (void)arg;
    winalp_log(WINALP_LOG_INFO, "thread_pool: AI thread started");

    /* Build system prompt once on start */
    rebuild_system_prompt();

    while (s_running) {
        /* Rebuild system prompt periodically (profile/plugins may change) */
        static int rebuild_counter;
        if (++rebuild_counter % 100 == 0)
            rebuild_system_prompt();

        /* Wait for a transcript */
        char transcript[MSG_SIZE];
        if (!queue_pop(&s_transcript_q, transcript, sizeof(transcript))) {
            Sleep(50);
            continue;
        }

        if (!ai_engine_is_loaded()) {
            queue_push(&s_ai_response_q, "(AI model not loaded)");
            continue;
        }

        /* Run inference with accumulator callback, crash-protected */
        AccumState as;
        as.buf[0] = '\0';
        as.len = 0;

        volatile int ai_crashed = 0;
        s_ai_thread_id = GetCurrentThreadId();
        PVOID veh = AddVectoredExceptionHandler(1, ai_veh);
        if (setjmp(s_ai_jmp) == 0) {
            ai_engine_infer(transcript, on_ai_token, &as);
        } else {
            ai_crashed = 1;
            winalp_log(WINALP_LOG_ERROR, "AI: crashed during inference");
        }
        RemoveVectoredExceptionHandler(veh);

        if (ai_crashed) {
            queue_push(&s_ai_response_q, "(AI engine crashed during inference)");
        } else if (as.len > 0) {
            queue_push(&s_ai_response_q, as.buf);
        } else {
            queue_push(&s_ai_response_q, "(empty response)");
        }
    }
    winalp_log(WINALP_LOG_INFO, "thread_pool: AI thread stopped");
    return 0;
}

/* ---------- Vision thread ---------- */
static void on_vision_result(const char *text, void *ud) {
    (void)ud;
    if (text && text[0])
        queue_push(&s_vision_q, text);
}

static DWORD WINAPI vision_thread(LPVOID arg) {
    (void)arg;
    winalp_log(WINALP_LOG_INFO, "thread_pool: Vision thread started");

    vision_engine_start_capture(on_vision_result, NULL);
    while (s_running) {
        vision_engine_poll();
        Sleep(200); /* 5 fps */
    }
    vision_engine_stop_capture();
    winalp_log(WINALP_LOG_INFO, "thread_pool: Vision thread stopped");
    return 0;
}

/* ---------- Memory/Plugin thread ---------- */
static DWORD WINAPI memory_thread(LPVOID arg) {
    (void)arg;
    winalp_log(WINALP_LOG_INFO, "thread_pool: Memory/Plugin thread started");

    while (s_running) {
        /* Periodic integrity check + cache prune */
        memory_store_integrity_check();
        plugin_manager_scan();
        Sleep(30000); /* every 30 seconds */
    }
    winalp_log(WINALP_LOG_INFO, "thread_pool: Memory/Plugin thread stopped");
    return 0;
}

/* ---------- public API ---------- */
void thread_pool_send_text(const char *text) {
    if (text && text[0])
        queue_push(&s_transcript_q, text);
}

bool thread_pool_start_all(void) {
    s_running = true;
    s_nthreads = 0;

    queue_init(&s_transcript_q);
    queue_init(&s_ai_response_q);
    queue_init(&s_vision_q);

    HANDLE t;

    t = CreateThread(NULL, 0, stt_thread, NULL, 0, NULL);
    if (t) { s_threads[s_nthreads++] = t; }

    t = CreateThread(NULL, 0, ai_thread, NULL, 0, NULL);
    if (t) { s_threads[s_nthreads++] = t; }

    t = CreateThread(NULL, 0, vision_thread, NULL, 0, NULL);
    if (t) { s_threads[s_nthreads++] = t; }

    t = CreateThread(NULL, 0, memory_thread, NULL, 0, NULL);
    if (t) { s_threads[s_nthreads++] = t; }

    winalp_log(WINALP_LOG_INFO, "thread_pool: %d threads started", s_nthreads);
    return s_nthreads > 0;
}

bool thread_pool_stop_all(void) {
    s_running = false;
    if (s_nthreads > 0) {
        WaitForMultipleObjects((DWORD)s_nthreads, s_threads, TRUE, 200);
        for (int i = 0; i < s_nthreads; i++)
            CloseHandle(s_threads[i]);
    }
    s_nthreads = 0;
    winalp_log(WINALP_LOG_INFO, "thread_pool: all threads stopped");
    return true;
}

bool thread_pool_is_running(void) {
    return s_running;
}

bool thread_pool_get_transcript(char *out, int out_len) {
    return queue_pop(&s_transcript_q, out, out_len);
}

bool thread_pool_get_ai_response(char *out, int out_len) {
    return queue_pop(&s_ai_response_q, out, out_len);
}

bool thread_pool_get_vision_text(char *out, int out_len) {
    return queue_pop(&s_vision_q, out, out_len);
}
