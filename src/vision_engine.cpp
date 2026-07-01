#include "../include/vision_engine.h"
#include "../include/ocr_engine.h"
#include "../include/logger.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static bool s_capturing;
static OcrResultCallback s_cb;
static void *s_ud;

/* Pixel-change detection */
static unsigned char *s_prev_frame;
static int s_prev_size;

/* D3D11 + DXGI duplication state */
static ID3D11Device           *s_d3d_device;
static ID3D11DeviceContext    *s_d3d_ctx;
static IDXGIOutputDuplication *s_dup;
static bool s_dxgi_ok; /* false → fall back to BitBlt */

static unsigned int simple_hash(const unsigned char *data, int len) {
    unsigned int h = 5381;
    for (int i = 0; i < len; i++)
        h = ((h << 5) + h) + (unsigned int)data[i];
    return h;
}

/* Initialise D3D11 device + DXGI OutputDuplication */
static bool init_dxgi_dup(void) {
    HRESULT hr;

    /* Create D3D11 device */
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, levels, 3,
                           D3D11_SDK_VERSION, &s_d3d_device, NULL, &s_d3d_ctx);
    if (FAILED(hr)) {
        /* Try WARP (software) fallback */
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, flags, levels, 3,
                               D3D11_SDK_VERSION, &s_d3d_device, NULL, &s_d3d_ctx);
        if (FAILED(hr)) return false;
    }

    /* Get DXGI device → adapter → output → duplication */
    IDXGIDevice *dxgiDev = NULL;
    hr = s_d3d_device->QueryInterface(IID_PPV_ARGS(&dxgiDev));
    if (FAILED(hr)) return false;

    IDXGIAdapter *adapter = NULL;
    hr = dxgiDev->GetAdapter(&adapter);
    dxgiDev->Release();
    if (FAILED(hr)) return false;

    IDXGIOutput *output = NULL;
    hr = adapter->EnumOutputs(0, &output);
    adapter->Release();
    if (FAILED(hr)) return false;

    /* Get IDXGIOutput1 for DuplicateOutput */
    IDXGIOutput1 *output1 = NULL;
    hr = output->QueryInterface(IID_PPV_ARGS(&output1));
    output->Release();
    if (FAILED(hr)) return false;

    /* Duplicate output */
    hr = output1->DuplicateOutput(s_d3d_device, &s_dup);
    output1->Release();
    if (FAILED(hr)) return false;

    winalp_log(WINALP_LOG_INFO, "vision_engine: DXGI OutputDuplication ready");
    return true;
}

bool vision_engine_init(void) {
    s_prev_frame = NULL;
    s_prev_size = 0;
    s_d3d_device = NULL;
    s_d3d_ctx = NULL;
    s_dup = NULL;

    s_dxgi_ok = init_dxgi_dup();
    if (!s_dxgi_ok)
        winalp_log(WINALP_LOG_WARN, "vision_engine: DXGI failed — using GDI BitBlt fallback");
    else
        winalp_log(WINALP_LOG_INFO, "vision_engine: initialised (DXGI)");
    return true;
}

void vision_engine_start_capture(OcrResultCallback cb, void *ud) {
    s_cb = cb;
    s_ud = ud;
    s_capturing = true;
    winalp_log(WINALP_LOG_INFO, "vision_engine: capture started");
}

/* Capture desktop via DXGI OutputDuplication */
static int capture_dxgi(unsigned char **out_data, int *out_w, int *out_h) {
    HRESULT hr;
    IDXGIResource *resource = NULL;
    DXGI_OUTDUPL_FRAME_INFO info;

    hr = s_dup->AcquireNextFrame(16, &info, &resource); /* 16ms ≈ 60fps timeout */
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return 0;  /* no new frame */
    if (FAILED(hr)) {
        /* Device lost — recreate */
        s_dup->Release(); s_dup = NULL;
        if (init_dxgi_dup()) {
            hr = s_dup->AcquireNextFrame(16, &info, &resource);
            if (FAILED(hr)) return 0;
        } else return 0;
    }

    /* Get the D3D11 texture from the resource */
    ID3D11Texture2D *tex = NULL;
    hr = resource->QueryInterface(IID_PPV_ARGS(&tex));
    resource->Release();
    if (FAILED(hr)) { s_dup->ReleaseFrame(); return 0; }

    /* Describe the texture */
    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);
    *out_w = (int)desc.Width;
    *out_h = (int)desc.Height;

    /* Create a staging texture for CPU read */
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage       = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags   = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags   = 0;

    ID3D11Texture2D *staging = NULL;
    hr = s_d3d_device->CreateTexture2D(&stagingDesc, NULL, &staging);
    if (FAILED(hr)) { tex->Release(); s_dup->ReleaseFrame(); return 0; }

    /* Copy GPU texture to staging */
    s_d3d_ctx->CopyResource(staging, tex);
    tex->Release();

    /* Map staging for CPU read */
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = s_d3d_ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) { staging->Release(); s_dup->ReleaseFrame(); return 0; }

    /* Copy pixels to output buffer (BGRA 32bpp, top-down) */
    int data_size = *out_w * *out_h * 4;
    *out_data = (unsigned char*)malloc((size_t)data_size);
    if (*out_data) {
        const unsigned char *src = (const unsigned char*)mapped.pData;
        /* DXGI gives bottom-up; we flip to top-down */
        for (int y = 0; y < *out_h; y++) {
            memcpy(*out_data + y * *out_w * 4,
                   src + (*out_h - 1 - y) * mapped.RowPitch,
                   (size_t)(*out_w) * 4);
        }
    }

    s_d3d_ctx->Unmap(staging, 0);
    staging->Release();
    s_dup->ReleaseFrame();

    return data_size;
}

/* Fallback: capture desktop via GDI BitBlt */
static int capture_bitblt(unsigned char **out_data, int *out_w, int *out_h) {
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
    bmi.bmiHeader.biHeight      = -*out_h;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    int data_size = (*out_w) * (*out_h) * 4;
    *out_data = (unsigned char*)malloc((size_t)data_size);
    if (*out_data)
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

    if (s_dxgi_ok)
        data_size = capture_dxgi(&frame, &w, &h);
    else
        data_size = capture_bitblt(&frame, &w, &h);

    if (data_size <= 0 || !frame) return;

    /* Pixel-change detection */
    unsigned int new_hash = simple_hash(frame, data_size);
    bool changed = true;
    if (s_prev_frame && s_prev_size == data_size) {
        unsigned int old_hash = simple_hash(s_prev_frame, s_prev_size);
        changed = (new_hash != old_hash);
    }

    if (!changed) {
        free(frame);
        return;
    }

    /* Screen changed — run OCR */
    free(s_prev_frame);
    s_prev_frame = frame;
    s_prev_size  = data_size;

    char *ocr_text = NULL;
    if (ocr_engine_recognize(frame, w, h, &ocr_text) && ocr_text) {
        if (s_cb) s_cb(ocr_text, s_ud);
        winalp_log(WINALP_LOG_INFO, "vision_engine: OCR: %s", ocr_text);
        ocr_engine_free_text(ocr_text);
    } else {
        /* OCR stub fallback */
        char result[256];
        snprintf(result, sizeof(result),
                 "(screen %dx%d, %d bytes, hash 0x%08x)", w, h, data_size, new_hash);
        if (s_cb) s_cb(result, s_ud);
    }
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
    if (s_dup)      { s_dup->Release(); s_dup = NULL; }
    if (s_d3d_ctx)  { s_d3d_ctx->Release(); s_d3d_ctx = NULL; }
    if (s_d3d_device) { s_d3d_device->Release(); s_d3d_device = NULL; }
    s_cb = NULL;
    winalp_log(WINALP_LOG_INFO, "vision_engine: shut down");
}
