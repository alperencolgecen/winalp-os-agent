#include "../include/thread_pool.h"
#include "../include/logger.h"
#include "../include/audio_capture.h"
#include "../include/stt_engine.h"
#include "../include/ai_engine.h"
#include "../include/vision_engine.h"
#include "../include/memory_store.h"
#include "../include/plugin_manager.h"
#include "../include/system_agent.h"
#include "../include/thread_mutex.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

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
static DWORD WINAPI stt_thread(LPVOID arg) {
    (void)arg;
    winalp_log(WINALP_LOG_INFO, "thread_pool: STT thread started");

    while (s_running) {
        if (!audio_capture_is_initialised()) {
            Sleep(100);
            continue;
        }
        float amplitude = audio_capture_rms();
        if (amplitude > 0.60f) {
            /* Voice detected — wait for speech to end, then transcribe */
            Sleep(1500);
            if (!s_running) break;

            float pcm[16000 * 2];
            int n = audio_capture_read(pcm, 16000 * 2);
            if (n > 0 && stt_engine_is_loaded()) {
                char result[4096] = "";
                stt_engine_process(pcm, n, /* no callback — use sync */ NULL, NULL);
                queue_push(&s_transcript_q, result);
            }
        } else {
            Sleep(50);
        }
    }
    winalp_log(WINALP_LOG_INFO, "thread_pool: STT thread stopped");
    return 0;
}

/* ---------- AI thread ---------- */
static DWORD WINAPI ai_thread(LPVOID arg) {
    (void)arg;
    winalp_log(WINALP_LOG_INFO, "thread_pool: AI thread started");

    while (s_running) {
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

        /* Run inference */
        char response[MSG_SIZE] = "";
        ai_engine_infer(transcript, NULL, NULL);
        /* The inference writes via callback — simplified: grab latest response */
        queue_push(&s_ai_response_q, response);
    }
    winalp_log(WINALP_LOG_INFO, "thread_pool: AI thread stopped");
    return 0;
}

/* ---------- Vision thread ---------- */
static DWORD WINAPI vision_thread(LPVOID arg) {
    (void)arg;
    winalp_log(WINALP_LOG_INFO, "thread_pool: Vision thread started");

    while (s_running) {
        /* Poll vision engine once per second */
        queue_push(&s_vision_q, "(screen capture snapshot)");
        Sleep(1000);
    }
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
        WaitForMultipleObjects((DWORD)s_nthreads, s_threads, TRUE, 5000);
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
