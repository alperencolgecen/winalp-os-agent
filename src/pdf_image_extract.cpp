#include "../include/pdf_reader.h"
#include "../include/ocr_engine.h"
#include "../include/logger.h"
#include <windows.h>
#include <wincodec.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>

/* ---------- internal helpers ---------- */

/* Locate the Nth page object definition in raw PDF and extract /XObject images */
static unsigned char *extract_page_image(const char *pdf_buf, size_t pdf_len,
                                          int page_num,
                                          int *out_w, int *out_h) {
    *out_w = *out_h = 0;

    /* Find page_num-th occurrence of "/Type /Page" or "/Type /XObject" */
    /* Scanned PDFs typically have one /XObject /Image per page */
    const char *p = pdf_buf;
    const char *end = pdf_buf + pdf_len;

    /* Approach: find the Nth /XObject <<...>> /Subtype /Image block */
    int page_idx = 0;
    while (p < end) {
        const char *xo = strstr(p, "/XObject");
        if (!xo || xo >= end) break;

        /* Look backwards for '<<' to find the dictionary start */
        const char *dict_start = xo;
        while (dict_start > pdf_buf && *dict_start != '<' && *(dict_start-1) != '<')
            dict_start--;
        if (dict_start > pdf_buf) dict_start--;

        /* Check if /Subtype /Image is in this dictionary */
        const char *subtype_check = xo;
        const char *subtype_end = strchr(subtype_check, '>');
        if (!subtype_end) subtype_end = strchr(subtype_check, '\n');
        if (!subtype_end) break;

        const char *st = strstr(subtype_check, "/Subtype");
        if (!st || st > subtype_end) { p = xo + 8; continue; }

        const char *st_val = st + 8;
        while (*st_val == ' ') st_val++;
        if (strncmp(st_val, "/Image", 6) != 0) { p = xo + 8; continue; }

        page_idx++;
        if (page_idx < page_num) { p = xo + 8; continue; }

        /* This is our target image. Now find the stream data. */
        /* Look for "stream" after the dictionary */
        const char *stream_start = strstr(subtype_end, "stream");
        if (!stream_start) { p = xo + 8; continue; }
        stream_start += 6; /* skip "stream" */
        while (stream_start < end && (*stream_start == '\r' || *stream_start == '\n'))
            stream_start++;

        /* Find "endstream" */
        const char *stream_end = strstr(stream_start, "endstream");
        if (!stream_end) { p = xo + 8; continue; }

        size_t stream_len = (size_t)(stream_end - stream_start);

        /* Determine the image filter */
        int is_jpeg = 0;
        int width = 0, height = 0;

        /* Parse image dictionary for /Width, /Height, /BitsPerComponent, /ColorSpace, /Filter */
        const char *dict_end = subtype_end;
        const char *dict_scan = xo - 20;
        if (dict_scan < pdf_buf) dict_scan = pdf_buf;

        /* Find /Width */
        const char *wpos = strstr(dict_scan, "/Width");
        if (wpos && wpos < dict_end) {
            wpos = strchr(wpos, ' '); if (wpos) width = atoi(wpos);
        }
        const char *hpos = strstr(dict_scan, "/Height");
        if (hpos && hpos < dict_end) {
            hpos = strchr(hpos, ' '); if (hpos) height = atoi(hpos);
        }
        const char *fpos = strstr(dict_scan, "/Filter");
        if (fpos && fpos < dict_end) {
            if (strstr(fpos, "DCTDecode")) is_jpeg = 1;
        }
        if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
            winalp_log(WINALP_LOG_WARN, "pdf_extract: invalid image dims %dx%d", width, height);
            p = xo + 8;
            continue;
        }

        if (is_jpeg) {
            /* Decode JPEG via WIC */
            IWICImagingFactory *factory = NULL;
            CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&factory));
            if (FAILED(hr) || !factory) {
                winalp_log(WINALP_LOG_WARN, "pdf_extract: WIC factory failed");
                p = xo + 8;
                continue;
            }

            IWICStream *stream = NULL;
            hr = factory->CreateStream(&stream);
            if (FAILED(hr)) { factory->Release(); p = xo + 8; continue; }

            hr = stream->InitializeFromMemory((BYTE*)stream_start, (DWORD)stream_len);
            if (FAILED(hr)) { stream->Release(); factory->Release(); p = xo + 8; continue; }

            IWICBitmapDecoder *decoder = NULL;
            hr = factory->CreateDecoderFromStream(stream, NULL, WICDecodeMetadataCacheOnDemand, &decoder);
            stream->Release();
            if (FAILED(hr) || !decoder) { factory->Release(); p = xo + 8; continue; }

            IWICBitmapFrameDecode *frame = NULL;
            hr = decoder->GetFrame(0, &frame);
            if (FAILED(hr) || !frame) { decoder->Release(); factory->Release(); p = xo + 8; continue; }

            UINT w = 0, h = 0;
            frame->GetSize(&w, &h);
            *out_w = (int)w;
            *out_h = (int)h;

            WICPixelFormatGUID fmt;
            frame->GetPixelFormat(&fmt);

            /* Convert to BGRA */
            IWICFormatConverter *conv = NULL;
            hr = factory->CreateFormatConverter(&conv);
            if (FAILED(hr) || !conv) {
                frame->Release(); decoder->Release(); factory->Release();
                p = xo + 8; continue;
            }

            hr = conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                                  WICBitmapDitherTypeNone, NULL, 0.0f, WICBitmapPaletteTypeCustom);
            frame->Release();
            decoder->Release();
            if (FAILED(hr)) { conv->Release(); factory->Release(); p = xo + 8; continue; }

            size_t row_pitch = (size_t)w * 4;
            size_t buf_size = row_pitch * (size_t)h;
            unsigned char *pixels = (unsigned char*)malloc(buf_size);
            if (!pixels) { conv->Release(); factory->Release(); p = xo + 8; continue; }

            WICRect rect = { 0, 0, (int)w, (int)h };
            hr = conv->CopyPixels(&rect, (UINT)row_pitch, (UINT)buf_size, pixels);
            conv->Release();
            factory->Release();

            if (FAILED(hr)) {
                free(pixels);
                winalp_log(WINALP_LOG_WARN, "pdf_extract: JPEG decode failed");
                p = xo + 8;
                continue;
            }

            winalp_log(WINALP_LOG_INFO, "pdf_extract: extracted JPEG img %dx%d page %d",
                       w, h, page_num);
            return pixels;
        }

        /* For non-JPEG / raw images — minimal support */
        /* Not implemented yet, return NULL */
        winalp_log(WINALP_LOG_WARN, "pdf_extract: non-JPEG image filter not supported on page %d",
                   page_num);
        return NULL;
    }

    return NULL;
}

/* ---------- public API ---------- */

unsigned char *pdf_reader_render_page(const char *pdf_path, int page,
                                      int *out_w, int *out_h) {
    if (!pdf_path || !out_w || !out_h) return NULL;
    *out_w = *out_h = 0;

    /* Read entire PDF into memory */
    FILE *f = fopen(pdf_path, "rb");
    if (!f) {
        winalp_log(WINALP_LOG_WARN, "pdf_extract: cannot open %s", pdf_path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return NULL; }
    rewind(f);

    char *buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);

    unsigned char *result = extract_page_image(buf, (size_t)sz, page, out_w, out_h);
    free(buf);
    return result;
}
