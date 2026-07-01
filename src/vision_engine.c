#include "../include/vision_engine.h"
#include "../include/logger.h"
#include <windows.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <string.h>

static bool s_capturing;
static OcrResultCallback s_cb;
static void *s_ud;

/* DXGI COM via dynamic vtable calls (MinGW-compatible) */
typedef HRESULT (WINAPI *CreateDXGIFactoryFn)(REFIID, void**);

bool vision_engine_init(void) {
    winalp_log(WINALP_LOG_INFO, "vision_engine: initialised");
    return true;
}

void vision_engine_start_capture(OcrResultCallback cb, void *ud) {
    s_cb = cb;
    s_ud = ud;
    s_capturing = true;
    winalp_log(WINALP_LOG_INFO, "vision_engine: capture started");
}

static void capture_single_frame(void) {
    HMODULE hDxgi = LoadLibraryA("dxgi.dll");
    if (!hDxgi) return;

    CreateDXGIFactoryFn fn = (CreateDXGIFactoryFn)GetProcAddress(hDxgi, "CreateDXGIFactory1");
    if (!fn) { FreeLibrary(hDxgi); return; }

    /* IID_IDXGIFactory1 = {770aae78-f26f-4dba-a829-253c83d1b387} */
    IID iidFactory = {0x770aae78,0xf26f,0x4dba,{0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87}};
    IDXGIFactory1 *pFactory = NULL;
    if (FAILED(fn(&iidFactory, (void**)&pFactory))) {
        FreeLibrary(hDxgi);
        return;
    }

    /* Enum adapter 0 */
    IDXGIAdapter1 *pAdapter = NULL;
    /* vtable[4] = EnumAdapters1 */
    HRESULT (STDMETHODCALLTYPE *enumAdap)(IDXGIFactory1*, UINT, IDXGIAdapter1**) =
        (void*)(((void***)pFactory)[0][4]);
    if (FAILED(enumAdap(pFactory, 0, &pAdapter))) {
        /* Release factory */
        ULONG (STDMETHODCALLTYPE *relFac)(IDXGIFactory1*) = (void*)(((void***)pFactory)[0][2]);
        relFac(pFactory);
        FreeLibrary(hDxgi);
        return;
    }

    /* Create IDXGIDevice from adapter (need D3D11 device) */
    /* For simplicity, use GetDesc to log adapter info and signal capture */
    DXGI_ADAPTER_DESC desc;
    HRESULT (STDMETHODCALLTYPE *getDesc)(IDXGIAdapter1*, DXGI_ADAPTER_DESC*) =
        (void*)(((void***)pAdapter)[0][5]);
    if (SUCCEEDED(getDesc(pAdapter, &desc))) {
        char gpu_name[128];
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, gpu_name, sizeof(gpu_name), NULL, NULL);
        winalp_log(WINALP_LOG_INFO, "vision_engine: adapter %s (%llu MB VRAM)",
                   gpu_name, desc.DedicatedVideoMemory / (1024*1024));
    }

    /* Release adapter */
    ULONG (STDMETHODCALLTYPE *relAdap)(IDXGIAdapter1*) = (void*)(((void***)pAdapter)[0][2]);
    relAdap(pAdapter);
    /* Release factory */
    ULONG (STDMETHODCALLTYPE *relFac)(IDXGIFactory1*) = (void*)(((void***)pFactory)[0][2]);
    relFac(pFactory);

    FreeLibrary(hDxgi);

    /* Callback with placeholder — full D3D11 device + duplication in later stage */
    if (s_cb)
        s_cb("(screen capture — DXGI placeholder)", s_ud);
}

void vision_engine_stop_capture(void) {
    s_capturing = false;
    winalp_log(WINALP_LOG_INFO, "vision_engine: capture stopped");
}

void vision_engine_shutdown(void) {
    s_capturing = false;
    s_cb = NULL;
    winalp_log(WINALP_LOG_INFO, "vision_engine: shut down");
}
