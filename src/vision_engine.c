#include "../include/vision_engine.h"
#include "../include/logger.h"
#include <windows.h>
#include <dxgi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SCREEN_W 1920
#define SCREEN_H 1080

static bool s_capturing;
static OcrResultCallback s_cb;
static void *s_ud;

/* Pixel-change detection */
static unsigned char *s_prev_frame;  /* hash of previous frame */
static int s_prev_size;

/* DXGI COM via dynamic vtable calls (MinGW-compatible) */
typedef HRESULT (WINAPI *CreateDXGIFactoryFn)(REFIID, void**);

static unsigned int simple_hash(const unsigned char *data, int len) {
    unsigned int h = 5381;
    for (int i = 0; i < len; i++)
        h = ((h << 5) + h) + (unsigned int)data[i];
    return h;
}

bool vision_engine_init(void) {
    s_prev_frame = NULL;
    s_prev_size = 0;
    winalp_log(WINALP_LOG_INFO, "vision_engine: initialised");
    return true;
}

void vision_engine_start_capture(OcrResultCallback cb, void *ud) {
    s_cb = cb;
    s_ud = ud;
    s_capturing = true;
    winalp_log(WINALP_LOG_INFO, "vision_engine: capture started");
}

/* Capture desktop via BitBlt (simple GDI fallback, no D3D11 device needed yet) */
static int capture_desktop(unsigned char **out_data, int *out_w, int *out_h) {
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem    = CreateCompatibleDC(hdcScreen);

    *out_w = GetDeviceCaps(hdcScreen, HORZRES);
    *out_h = GetDeviceCaps(hdcScreen, VERTRES);

    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, *out_w, *out_h);
    SelectObject(hdcMem, hBmp);

    BitBlt(hdcMem, 0, 0, *out_w, *out_h, hdcScreen, 0, 0, SRCCOPY);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = *out_w;
    bmi.bmiHeader.biHeight      = -*out_h;  /* top-down */
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    int data_size = (*out_w) * (*out_h) * 4;
    *out_data = (unsigned char*)malloc((size_t)data_size);
    if (!*out_data) {
        DeleteObject(hBmp); DeleteDC(hdcMem); ReleaseDC(NULL, hdcScreen);
        return 0;
    }

    GetDIBits(hdcMem, hBmp, 0, (UINT)*out_h, *out_data, &bmi, DIB_RGB_COLORS);

    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return data_size;
}

void vision_engine_poll(void) {
    if (!s_capturing) return;

    unsigned char *frame = NULL;
    int w, h, data_size;

    data_size = capture_desktop(&frame, &w, &h);
    if (data_size <= 0 || !frame) return;

    /* Pixel-change detection via hash comparison */
    unsigned int new_hash = simple_hash(frame, data_size);

    if (s_prev_frame && s_prev_size == data_size) {
        unsigned int old_hash = simple_hash(s_prev_frame, s_prev_size);
        if (new_hash == old_hash) {
            /* screen unchanged — skip OCR */
            free(frame);
            return;
        }
    }

    /* Screen changed — run OCR (stub) */
    free(s_prev_frame);
    s_prev_frame = frame;
    s_prev_size  = data_size;

    /* OCR placeholder: report pixel count as proof of concept */
    char result[256];
    snprintf(result, sizeof(result),
             "(screen %dx%d, %d bytes, hash 0x%08x — OCR stub)",
             w, h, data_size, new_hash);

    if (s_cb)
        s_cb(result, s_ud);

    winalp_log(WINALP_LOG_INFO, "vision_engine: screen changed — %s", result);
}

void vision_engine_stop_capture(void) {
    s_capturing = false;
    free(s_prev_frame);
    s_prev_frame = NULL;
    s_prev_size = 0;
    winalp_log(WINALP_LOG_INFO, "vision_engine: capture stopped");
}

void vision_engine_shutdown(void) {
    s_capturing = false;
    free(s_prev_frame);
    s_prev_frame = NULL;
    s_prev_size = 0;
    s_cb = NULL;
    winalp_log(WINALP_LOG_INFO, "vision_engine: shut down");
}
