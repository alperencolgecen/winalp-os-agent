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

    /* Enumerate voices and try to select Turkish (TR, LANGID 0x041F) */
    ISpObjectTokenCategory *cat = NULL;
    hr = CoCreateInstance(&CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL,
                          &IID_ISpObjectTokenCategory, (void**)&cat);
    if (SUCCEEDED(hr)) {
        cat->lpVtbl->SetId(cat, L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices", FALSE);
        IEnumSpObjectTokens *enum_tokens = NULL;
        hr = cat->lpVtbl->EnumTokens(cat, NULL, NULL, &enum_tokens);
        if (SUCCEEDED(hr) && enum_tokens) {
            ISpObjectToken *token;
            int found_tr = -1, first = -1;
            int idx = 0;
            while (enum_tokens->lpVtbl->Next(enum_tokens, 1, &token, NULL) == S_OK) {
                wchar_t *name = NULL;
                token->lpVtbl->GetStringValue(token, L"", &name);
                char name_a[256];
                if (name) {
                    WideCharToMultiByte(CP_UTF8, 0, name, -1, name_a, sizeof(name_a), NULL, NULL);
                    winalp_log(WINALP_LOG_INFO, "tts: voice[%d]: %s", idx, name_a);
                    CoTaskMemFree(name);
                }
                if (first < 0) first = idx;
                /* Check language: primary LANG is 0x041F = Turkish */
                ISpDataKey *attrs = NULL;
                token->lpVtbl->OpenKey(token, L"Attributes", &attrs);
                if (attrs) {
                    wchar_t *lang = NULL;
                    attrs->lpVtbl->GetStringValue(attrs, L"Language", &lang);
                    if (lang) {
                        if (wcsstr(lang, L"041F") || wcsstr(lang, L"TR") ||
                            wcsstr(lang, L"tr")) {
                            found_tr = idx;
                            winalp_log(WINALP_LOG_INFO, "tts: Turkish voice found: %s", name_a);
                            CoTaskMemFree(lang);
                            attrs->lpVtbl->Release(attrs);
                            CoTaskMemFree(name);
                            token->lpVtbl->Release(token);
                            idx++;
                            break;
                        }
                        CoTaskMemFree(lang);
                    }
                    attrs->lpVtbl->Release(attrs);
                }
                token->lpVtbl->Release(token);
                idx++;
            }
            enum_tokens->lpVtbl->Release(enum_tokens);

            /* Select Turkish voice or keep default */
            if (found_tr >= 0) {
                enum_tokens = NULL;
                cat->lpVtbl->EnumTokens(cat, NULL, NULL, &enum_tokens);
                if (enum_tokens) {
                    idx = 0;
                    while (enum_tokens->lpVtbl->Next(enum_tokens, 1, &token, NULL) == S_OK) {
                        if (idx == found_tr) {
                            s_voice->lpVtbl->SetVoice(s_voice, token);
                            wchar_t *n2 = NULL;
                            token->lpVtbl->GetStringValue(token, L"", &n2);
                            char n2_a[256];
                            if (n2) {
                                WideCharToMultiByte(CP_UTF8, 0, n2, -1, n2_a, sizeof(n2_a), NULL, NULL);
                                winalp_log(WINALP_LOG_INFO, "tts: selected Turkish: %s", n2_a);
                                CoTaskMemFree(n2);
                            }
                            token->lpVtbl->Release(token);
                            break;
                        }
                        token->lpVtbl->Release(token);
                        idx++;
                    }
                    enum_tokens->lpVtbl->Release(enum_tokens);
                }
            }
        }
        cat->lpVtbl->Release(cat);
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
