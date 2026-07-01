/* ocr_engine.cpp — Windows built-in OCR via direct WinRT COM */
#include <windows.h>
#include <roapi.h>
#include <windows.graphics.imaging.h>
#include <wincodec.h>
#include <cstdio>
#include <cstring>

/* ========== Manually defined OCR COM interfaces ========== */

/* {9F0EFD6E-E41E-46F5-ADA4-7D12B498A2EC} */
static const GUID IID_IOcrEngineStatics = {
    0x9f0efd6e, 0xe41e, 0x46f5, {0xad, 0xa4, 0x7d, 0x12, 0xb4, 0x98, 0xa2, 0xec}
};
/* {1573C7E0-6CD2-43AE-AE18-68E8A2A4FA9B} */
static const GUID IID_IOcrEngine = {
    0x1573c7e0, 0x6cd2, 0x43ae, {0xae, 0x18, 0x68, 0xe8, 0xa2, 0xa4, 0xfa, 0x9b}
};
/* {9E5F1429-080B-4C48-A5DD-87AAA5305195} */
static const GUID IID_IOcrResult = {
    0x9e5f1429, 0x80b, 0x4c48, {0xa5, 0xdd, 0x87, 0xaa, 0xa5, 0x30, 0x51, 0x95}
};

/* OcrResult interface */
struct IOcrResult : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_Text(HSTRING *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Lines(void **value) = 0;
};

/* OcrEngine interface */
struct IOcrEngine : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_MaxImageDimension(int *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE RecognizeAsync(IUnknown *bitmap, void **operation) = 0;
};

/* OcrEngine statics (factory) */
struct IOcrEngineStatics : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE TryCreateFromLanguage(IUnknown *language, IOcrEngine **engine) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryCreateFromUserProfileLanguages(IOcrEngine **engine) = 0;
};

/* AsyncOperation<OcrResult> */
struct IAsyncOperation_OcrResult : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(IOcrResult **results) = 0;
};

/* ========== Internal state ========== */
static bool s_initialised;
static HMODULE s_hComBase;
static bool s_com_init;

/* Ro* function pointers */
typedef HRESULT (WINAPI *RoInitializeFn)(RO_INIT_TYPE);
typedef void (WINAPI *RoUninitializeFn)(void);
typedef HRESULT (WINAPI *RoGetActivationFactoryFn)(HSTRING activatableClassId, const IID &iid, void **factory);
typedef HRESULT (WINAPI *WindowsCreateStringFn)(const wchar_t *sourceString, UINT32 length, HSTRING *string);
typedef HRESULT (WINAPI *WindowsDeleteStringFn)(HSTRING string);
typedef const wchar_t* (WINAPI *WindowsGetStringRawBufferFn)(HSTRING string, UINT32 *length);

static RoInitializeFn            pRoInitialize;
static RoUninitializeFn          pRoUninitialize;
static RoGetActivationFactoryFn  pRoGetActivationFactory;
static WindowsCreateStringFn     pWindowsCreateString;
static WindowsDeleteStringFn     pWindowsDeleteString;
static WindowsGetStringRawBufferFn pWindowsGetStringRawBuffer;

static HRESULT make_hstring(const wchar_t *src, HSTRING *out) {
    return pWindowsCreateString(src, (UINT32)wcslen(src), out);
}

static HRESULT load_winrt_apis(void) {
    s_hComBase = LoadLibraryW(L"combase.dll");
    if (!s_hComBase) return E_FAIL;
    pRoInitialize            = (RoInitializeFn)GetProcAddress(s_hComBase, "RoInitialize");
    pRoUninitialize          = (RoUninitializeFn)GetProcAddress(s_hComBase, "RoUninitialize");
    pRoGetActivationFactory  = (RoGetActivationFactoryFn)GetProcAddress(s_hComBase, "RoGetActivationFactory");
    pWindowsCreateString     = (WindowsCreateStringFn)GetProcAddress(s_hComBase, "WindowsCreateString");
    pWindowsDeleteString     = (WindowsDeleteStringFn)GetProcAddress(s_hComBase, "WindowsDeleteString");
    pWindowsGetStringRawBuffer = (WindowsGetStringRawBufferFn)GetProcAddress(s_hComBase, "WindowsGetStringRawBuffer");
    if (!pRoInitialize || !pRoUninitialize || !pRoGetActivationFactory ||
        !pWindowsCreateString || !pWindowsDeleteString || !pWindowsGetStringRawBuffer)
        return E_FAIL;
    return S_OK;
}

/* Helper: create HSTRING from wide string, call function to get raw buffer */
static const wchar_t *hstr_raw(HSTRING h, UINT32 *len) {
    return pWindowsGetStringRawBuffer(h, len);
}

bool ocr_engine_init(void) {
    if (s_initialised) return true;
    if (FAILED(load_winrt_apis())) return false;
    HRESULT hr = pRoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) return false;
    s_com_init = true;
    s_initialised = true;
    return true;
}

/* Convert captured BGRA bitmap to SoftwareBitmap via WIC + WinRT */
static IUnknown *create_software_bitmap(const unsigned char *bitmap, int width, int height) {
    IUnknown *result = NULL;
    HRESULT hr;

    IWICImagingFactory *wicFactory = NULL;
    hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) return NULL;

    IWICBitmap *wicBitmap = NULL;
    hr = wicFactory->CreateBitmapFromMemory((UINT)width, (UINT)height, GUID_WICPixelFormat32bppBGRA,
                                            (UINT)width * 4, (UINT)height * width * 4,
                                            (BYTE*)bitmap, &wicBitmap);
    if (FAILED(hr)) { wicFactory->Release(); return NULL; }

    /* Get SoftwareBitmap statics factory */
    HSTRING imgFactoryId = NULL;
    hr = make_hstring(L"Windows.Graphics.Imaging.SoftwareBitmap", &imgFactoryId);
    if (FAILED(hr)) { wicBitmap->Release(); wicFactory->Release(); return NULL; }

    IUnknown *sbFactory = NULL;
    hr = pRoGetActivationFactory(imgFactoryId, IID_PPV_ARGS(&sbFactory));
    pWindowsDeleteString(imgFactoryId);
    if (FAILED(hr)) { wicBitmap->Release(); wicFactory->Release(); return NULL; }

    /* ISoftwareBitmapStatics IID */
    static const IID IID_ISoftwareBitmapStatics = {
        0xdf824200, 0x8558, 0x47fb, {0xa8, 0x8f, 0x17, 0x20, 0x9a, 0xd4, 0x09, 0xa5}
    };
    IUnknown *sbStatics = NULL;
    hr = sbFactory->QueryInterface(IID_ISoftwareBitmapStatics, (void**)&sbStatics);
    sbFactory->Release();
    if (FAILED(hr) || !sbStatics) { wicBitmap->Release(); wicFactory->Release(); return NULL; }

    /* vtable slot 6 = CreateCopyFromBitmap (after IUnknown's 3 + 3 statics methods) */
    typedef HRESULT (STDMETHODCALLTYPE *CreateFromWicFn)(IUnknown*, IWICBitmap*, IUnknown**);
    void **svtab = *(void***)sbStatics;
    CreateFromWicFn fn = (CreateFromWicFn)svtab[6];
    hr = fn(sbStatics, wicBitmap, &result);
    sbStatics->Release();
    wicBitmap->Release();
    wicFactory->Release();
    return SUCCEEDED(hr) ? result : NULL;
}

bool ocr_engine_recognize(const unsigned char *bitmap, int width, int height, char **out_text) {
    if (!s_initialised || !bitmap || !out_text) return false;
    *out_text = NULL;
    HRESULT hr;

    /* Step 1: Create OCR engine */
    HSTRING ocrClassId = NULL;
    hr = make_hstring(L"Windows.Media.Ocr.OcrEngine", &ocrClassId);
    if (FAILED(hr)) return false;

    IOcrEngineStatics *engineStatics = NULL;
    hr = pRoGetActivationFactory(ocrClassId, IID_IOcrEngineStatics, (void**)&engineStatics);
    pWindowsDeleteString(ocrClassId);
    if (FAILED(hr) || !engineStatics) return false;

    IOcrEngine *ocrEngine = NULL;
    hr = engineStatics->TryCreateFromUserProfileLanguages(&ocrEngine);
    engineStatics->Release();
    if (FAILED(hr) || !ocrEngine) return false;

    /* Step 2: Convert bitmap to SoftwareBitmap */
    IUnknown *sb = create_software_bitmap(bitmap, width, height);
    if (!sb) { ocrEngine->Release(); return false; }

    /* Step 3: Recognize asynchronously and get results */
    IAsyncOperation_OcrResult *asyncOp = NULL;
    hr = ocrEngine->RecognizeAsync(sb, (void**)&asyncOp);
    sb->Release();
    ocrEngine->Release();
    if (FAILED(hr) || !asyncOp) return false;

    IOcrResult *ocrResult = NULL;
    hr = asyncOp->GetResults(&ocrResult);
    asyncOp->Release();
    if (SUCCEEDED(hr) && ocrResult) {
        HSTRING text = NULL;
        hr = ocrResult->get_Text(&text);
        if (SUCCEEDED(hr) && text) {
            UINT32 len = 0;
            const wchar_t *ws = hstr_raw(text, &len);
            if (ws && len > 0) {
                int needed = WideCharToMultiByte(CP_UTF8, 0, ws, (int)len, NULL, 0, NULL, NULL);
                if (needed > 0) {
                    *out_text = (char*)malloc((size_t)needed + 1);
                    if (*out_text) {
                        WideCharToMultiByte(CP_UTF8, 0, ws, (int)len, *out_text, needed, NULL, NULL);
                        (*out_text)[needed] = '\0';
                    }
                }
            }
            pWindowsDeleteString(text);
        }
        ocrResult->Release();
    }
    return *out_text != NULL;
}

void ocr_engine_free_text(char *text) {
    free(text);
}

void ocr_engine_shutdown(void) {
    if (s_com_init) { pRoUninitialize(); s_com_init = false; }
    if (s_hComBase) { FreeLibrary(s_hComBase); s_hComBase = NULL; }
    s_initialised = false;
}
