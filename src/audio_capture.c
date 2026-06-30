#define MINIAUDIO_IMPLEMENTATION
#include "../include/miniaudio.h"
#include "../include/audio_capture.h"
#include "../include/logger.h"

#define SAMPLE_RATE   16000
#define BUF_SECONDS   5
#define BUF_SIZE      (SAMPLE_RATE * BUF_SECONDS)   /* 80000 */
#define RMS_WINDOW    (SAMPLE_RATE / 4)             /* 250ms */

static ma_device      s_device;
static float          s_ring[BUF_SIZE];
static volatile int   s_writePos;                   /* producer index */
static int            s_readPos;                    /* consumer index (snapshot) */
static CRITICAL_SECTION s_lock;
static bool           s_running = false;

static void capture_cb(ma_device *dev, void *out, const void *in, ma_uint32 frames) {
    (void)dev; (void)out;
    if (!in) return;

    const float *input = (const float*)in;
    EnterCriticalSection(&s_lock);
    for (ma_uint32 i = 0; i < frames; i++) {
        s_ring[s_writePos] = input[i * dev->capture.channels];
        s_writePos = (s_writePos + 1) % BUF_SIZE;
    }
    LeaveCriticalSection(&s_lock);
}

bool audio_capture_start(void) {
    if (s_running) return true;

    InitializeCriticalSection(&s_lock);
    s_writePos = 0;
    s_readPos  = 0;

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format    = ma_format_f32;
    cfg.capture.channels  = 1;
    cfg.sampleRate        = SAMPLE_RATE;
    cfg.dataCallback      = capture_cb;

    if (ma_device_init(NULL, &cfg, &s_device) != MA_SUCCESS) {
        winalp_log(WINALP_LOG_ERROR, "audio_capture: ma_device_init failed (no mic?)");
        DeleteCriticalSection(&s_lock);
        return false;
    }

    if (ma_device_start(&s_device) != MA_SUCCESS) {
        winalp_log(WINALP_LOG_ERROR, "audio_capture: ma_device_start failed");
        ma_device_uninit(&s_device);
        DeleteCriticalSection(&s_lock);
        return false;
    }

    s_running = true;
    winalp_log(WINALP_LOG_INFO, "audio_capture: started (%d Hz, mono, f32)", SAMPLE_RATE);
    return true;
}

void audio_capture_stop(void) {
    if (!s_running) return;
    ma_device_stop(&s_device);
    ma_device_uninit(&s_device);
    DeleteCriticalSection(&s_lock);
    s_running = false;
    winalp_log(WINALP_LOG_INFO, "audio_capture: stopped");
}

float audio_capture_rms(void) {
    EnterCriticalSection(&s_lock);
    int wp = s_writePos;
    LeaveCriticalSection(&s_lock);

    double sum = 0.0;
    int n = RMS_WINDOW;
    for (int i = 0; i < n; i++) {
        int idx = (wp - n + i) % BUF_SIZE;
        if (idx < 0) idx += BUF_SIZE;
        float s = s_ring[idx];
        sum += (double)(s * s);
    }
    return (float)sqrt(sum / n);
}

int audio_capture_read(float *buf, int max_samples) {
    EnterCriticalSection(&s_lock);
    int wp = s_writePos;
    LeaveCriticalSection(&s_lock);

    int available = (wp - s_readPos + BUF_SIZE) % BUF_SIZE;
    int to_copy = (available < max_samples) ? available : max_samples;

    for (int i = 0; i < to_copy; i++) {
        buf[i] = s_ring[(s_readPos + i) % BUF_SIZE];
    }
    s_readPos = (s_readPos + to_copy) % BUF_SIZE;
    return to_copy;
}
