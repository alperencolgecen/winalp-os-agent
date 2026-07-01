#include "../include/sys_diag.h"
#include "../include/logger.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <intrin.h>

/* DXGI for VRAM detection */
#include <dxgi.h>

bool sys_diag_detect(SysDiag *d) {
    if (!d) return false;
    memset(d, 0, sizeof(SysDiag));

    /* CPU cores/threads */
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    d->cpu_cores   = si.dwNumberOfProcessors;
    d->cpu_threads = (int)si.dwNumberOfProcessors;

    /* CPU name via CPUID */
    int cpuInfo[4] = {0};
    __cpuid(cpuInfo, 0x80000000);
    if (cpuInfo[0] >= 0x80000004) {
        char buf[49] = {0};
        for (unsigned int e = 0x80000002; e <= 0x80000004; e++) {
            __cpuid(cpuInfo, (int)e);
            memcpy(buf + (size_t)((e - 0x80000002) * 16), cpuInfo, 16);
        }
        /* trim leading/trailing spaces */
        char *s = buf, *e2 = buf + strlen(buf) - 1;
        while (*s == ' ') s++;
        while (e2 > s && *e2 == ' ') *e2-- = '\0';
        strncpy(d->cpu_name, s, sizeof(d->cpu_name) - 1);
    } else {
        strncpy(d->cpu_name, "Unknown CPU", sizeof(d->cpu_name) - 1);
    }

    /* RAM */
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        d->ram_total_mb = (unsigned long long)(mem.ullTotalPhys / (1024 * 1024));
        d->ram_free_mb  = (unsigned long long)(mem.ullAvailPhys / (1024 * 1024));
    }

    /* VRAM via DXGI — use COM directly with explicit vtable calls */
    {
        typedef HRESULT (WINAPI *CreateDXGIFactoryFn)(REFIID, void**);
        HMODULE hDxgi = LoadLibraryA("dxgi.dll");
        if (hDxgi) {
            CreateDXGIFactoryFn fn = (CreateDXGIFactoryFn)GetProcAddress(hDxgi, "CreateDXGIFactory");
            if (fn) {
                IDXGIFactory *pFactory = NULL;
                IID iid = {0x7b7166ec,0x21c7,0x44ae,{0xb2,0x1a,0xc9,0xae,0x32,0x1a,0xe3,0x69}};
                if (SUCCEEDED(fn(&iid, (void**)&pFactory))) {
                    IDXGIAdapter *pAdapter = NULL;
                    /* vtable call: EnumAdapters */
                    HRESULT (STDMETHODCALLTYPE *enumFn)(IDXGIFactory*, UINT, IDXGIAdapter**) =
                        (void*)pFactory->lpVtbl;
                    if (SUCCEEDED(enumFn(pFactory, 0, &pAdapter))) {
                        DXGI_ADAPTER_DESC desc;
                        /* vtable call: GetDesc */
                        HRESULT (STDMETHODCALLTYPE *getDescFn)(IDXGIAdapter*, DXGI_ADAPTER_DESC*) =
                            (void*)pAdapter->lpVtbl;
                        if (SUCCEEDED(getDescFn(pAdapter, &desc))) {
                            int len = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                                                           d->gpu_name, (int)sizeof(d->gpu_name),
                                                           NULL, NULL);
                            if (len > 0) d->gpu_name[sizeof(d->gpu_name) - 1] = '\0';
                            d->vram_total_mb = (unsigned long long)(desc.DedicatedVideoMemory / (1024 * 1024));
                            d->vram_free_mb  = d->vram_total_mb;
                        }
                        /* vtable call: Release */
                        ULONG (STDMETHODCALLTYPE *relFn)(IDXGIAdapter*) = (void*)(((void***)pAdapter)[0][2]);
                        relFn(pAdapter);
                    }
                    /* vtable call: Release on factory */
                    ULONG (STDMETHODCALLTYPE *relFacFn)(IDXGIFactory*) = (void*)(((void***)pFactory)[0][2]);
                    relFacFn(pFactory);
                }
            }
            FreeLibrary(hDxgi);
        }
    }

    /* Determine model tier */
    unsigned long long total_mb = d->ram_total_mb + d->vram_total_mb;
    if (total_mb > 16000)       d->model_tier = 3; /* heavy: 14B+ */
    else if (total_mb > 4000)   d->model_tier = 2; /* medium: 3B */
    else                        d->model_tier = 1; /* light: 1.5B */

    return true;
}

void sys_diag_print(const SysDiag *d) {
    if (!d) return;
    winalp_log(WINALP_LOG_INFO, "CPU: %s (%d cores/%d threads)",
               d->cpu_name, d->cpu_cores, d->cpu_threads);
    winalp_log(WINALP_LOG_INFO, "RAM: %llu MB total, %llu MB free",
               d->ram_total_mb, d->ram_free_mb);
    if (d->vram_total_mb > 0)
        winalp_log(WINALP_LOG_INFO, "GPU: %s (%llu MB VRAM)",
                   d->gpu_name, d->vram_total_mb);
    winalp_log(WINALP_LOG_INFO, "Recommended model tier: %d (1=light,2=medium,3=heavy)",
               d->model_tier);
}
