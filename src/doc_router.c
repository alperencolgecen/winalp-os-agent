#include "../include/doc_router.h"
#include "../include/ocr_engine.h"
#include "../include/vision_engine.h"
#include "../include/ai_engine.h"
#include "../include/vlm_engine.h"
#include "../include/logger.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>

static bool s_vlm_available;
static char s_mmproj_path[1024];
static char s_models_dir[1024];

/* Callback context for collecting VLM token output */
struct CollectCtx {
    char *buf;
    int pos;
    int cap;
};

static void collect_cb(const char *token, void *userdata) {
    struct CollectCtx *ctx = (struct CollectCtx*)userdata;
    if (!ctx || !token) return;
    int n = (int)strlen(token);
    if (ctx->pos + n < ctx->cap - 1) {
        memcpy(ctx->buf + ctx->pos, token, (size_t)n);
        ctx->pos += n;
        ctx->buf[ctx->pos] = '\0';
    }
}

/* Forward: pdf_reader minimal text extraction (returns allocated string) */
char *pdf_reader_extract_text(const char *pdf_path);

bool doc_router_init(const char *models_dir) {
    if (!models_dir) return false;
    strncpy(s_models_dir, models_dir, sizeof(s_models_dir) - 1);

    /* Scan for mmproj-*.gguf files */
    s_vlm_available = false;
    char search[1024];
    snprintf(search, sizeof(search), "%s\\mmproj-*.gguf", models_dir);

    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFileA(search, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        snprintf(s_mmproj_path, sizeof(s_mmproj_path), "%s\\%s", models_dir, fd.cFileName);
        s_vlm_available = true;
        FindClose(h);
        winalp_log(WINALP_LOG_INFO, "doc_router: VLM mmproj found: %s", s_mmproj_path);
        /* Initialise VLM engine — will load mmproj via mtmd */
        if (ai_engine_is_loaded() && ai_engine_is_vlm()) {
            if (vlm_engine_init(s_mmproj_path)) {
                winalp_log(WINALP_LOG_INFO, "doc_router: VLM engine ready");
            } else {
                winalp_log(WINALP_LOG_WARN, "doc_router: VLM engine init failed — OCR fallback only");
            }
        } else {
            winalp_log(WINALP_LOG_INFO, "doc_router: model not VLM — mmproj loaded for future use");
        }
    } else {
        winalp_log(WINALP_LOG_INFO, "doc_router: no mmproj found — OCR-only mode");
    }

    /* Initialise OCR engine */
    if (!ocr_engine_init()) {
        winalp_log(WINALP_LOG_WARN, "doc_router: OCR engine init failed (Windows <10?)");
    }

    return true;
}

bool doc_router_is_vlm_available(void) {
    return s_vlm_available;
}

bool doc_router_process_image(const unsigned char *bitmap, int width, int height,
                               char **out_text) {
    if (!bitmap || !out_text) return false;
    *out_text = NULL;

    if (s_vlm_available && ai_engine_is_vlm() && vlm_engine_is_ready()) {
        /* VLM path — convert BGRA 32bpp → RGB 24bpp, process via mtmd */
        int n_pixels = width * height;
        unsigned char *rgb = (unsigned char*)malloc((size_t)n_pixels * 3);
        if (rgb) {
            vlm_engine_bgra_to_rgb(bitmap, width, height, rgb);
            char vlm_buf[32768];
            struct CollectCtx vlm_ctx = { .buf = vlm_buf, .pos = 0, .cap = (int)sizeof(vlm_buf) };
            bool ok = vlm_engine_process("Describe this image in detail.",
                                          rgb, width, height,
                                          collect_cb, &vlm_ctx);
            free(rgb);
            if (ok && vlm_ctx.pos > 0) {
                *out_text = (char*)malloc((size_t)vlm_ctx.pos + 1);
                if (*out_text) {
                    memcpy(*out_text, vlm_buf, (size_t)vlm_ctx.pos);
                    (*out_text)[vlm_ctx.pos] = '\0';
                }
                winalp_log(WINALP_LOG_INFO, "doc_router: VLM returned %d chars", vlm_ctx.pos);
                return true;
            }
            winalp_log(WINALP_LOG_WARN, "doc_router: VLM returned no content — OCR fallback");
        }
    } else if (s_vlm_available && !ai_engine_is_vlm()) {
        winalp_log(WINALP_LOG_INFO, "doc_router: mmproj found but model is not VLM");
    }

    /* OCR fallback — use Windows built-in OCR engine */
    if (ocr_engine_recognize(bitmap, width, height, out_text)) {
        winalp_log(WINALP_LOG_INFO, "doc_router: OCR recognized %zu chars",
                   *out_text ? strlen(*out_text) : 0);
        return true;
    }

    winalp_log(WINALP_LOG_WARN, "doc_router: OCR returned no text");
    return false;
}

/* Forward: pdf_reader_render_page from pdf_image_extract.cpp */
unsigned char *pdf_reader_render_page(const char *pdf_path, int page,
                                       int *out_w, int *out_h);

bool doc_router_process_pdf_page(const char *pdf_path, int page_num, char **out_text) {
    if (!pdf_path || !out_text) return false;
    *out_text = NULL;

    /* Try text extraction first */
    char *text = pdf_reader_extract_text(pdf_path);
    if (text) {
        *out_text = text;
        return true;
    }

    /* Text extraction failed — try rendering page as image and OCR */
    int w = 0, h = 0;
    unsigned char *pixels = pdf_reader_render_page(pdf_path, page_num, &w, &h);
    if (pixels && w > 0 && h > 0) {
        winalp_log(WINALP_LOG_INFO, "doc_router: rendered PDF page %d to image %dx%d",
                   page_num, w, h);
        bool ok = doc_router_process_image(pixels, w, h, out_text);
        free(pixels);
        if (ok) return true;
    }

    winalp_log(WINALP_LOG_INFO, "doc_router: PDF page %d yielded no content (text nor image)", page_num);
    return false;
}

void doc_router_shutdown(void) {
    vlm_engine_shutdown();
    ocr_engine_shutdown();
    s_vlm_available = false;
}
