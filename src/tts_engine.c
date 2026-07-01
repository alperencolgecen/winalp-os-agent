#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sapi.h>
#include "../include/tts_engine.h"
#include "../include/logger.h"

static ISpVoice *s_voice = NULL;

bool tts_engine_init(void) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        winalp_log(WINALP_LOG_ERROR, "tts: CoInitializeEx failed: 0x%lx", hr);
        return false;
    }

    hr = CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL, &IID_ISpVoice, (void**)&s_voice);
    if (FAILED(hr)) {
        winalp_log(WINALP_LOG_ERROR, "tts: CoCreateInstance ISpVoice failed: 0x%lx", hr);
        return false;
    }

    winalp_log(WINALP_LOG_INFO, "tts: SAPI voice ready");
    return true;
}

void tts_engine_speak_async(const char *text) {
    if (!s_voice || !text || !text[0]) return;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (wlen <= 0) return;

    wchar_t *wtext = (wchar_t*)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wtext) return;

    MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, wlen);
    s_voice->lpVtbl->Speak(s_voice, wtext, SPF_ASYNC, NULL);
    free(wtext);
}

bool tts_engine_is_speaking(void) {
    if (!s_voice) return false;
    SPVOICESTATUS status;
    s_voice->lpVtbl->GetStatus(s_voice, &status, NULL);
    return status.dwRunningState == SPRS_IS_SPEAKING;
}

void tts_engine_stop(void) {
    if (s_voice) s_voice->lpVtbl->Speak(s_voice, NULL, SPF_PURGEBEFORESPEAK, NULL);
}

void tts_engine_shutdown(void) {
    if (s_voice) { s_voice->lpVtbl->Release(s_voice); s_voice = NULL; }
    CoUninitialize();
    winalp_log(WINALP_LOG_INFO, "tts: shutdown");
}
