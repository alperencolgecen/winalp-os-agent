#include "../include/vlm_engine.h"
#include "../include/ai_engine.h"
#include "../include/logger.h"
#include "mtmd.h"
#include "mtmd-helper.h"
#include "llama.h"
#include <stdlib.h>
#include <string.h>

static mtmd_context *s_mctx = NULL;
static bool s_ready = false;

bool vlm_engine_init(const char *mmproj_path) {
    if (!mmproj_path) return false;

    struct llama_model *model = ai_engine_get_model();
    if (!model) {
        winalp_log(WINALP_LOG_ERROR, "VLM: no text model loaded");
        return false;
    }

    struct mtmd_context_params params = mtmd_context_params_default();
    params.n_threads = 4;
    params.use_gpu = true;

    s_mctx = mtmd_init_from_file(mmproj_path, model, params);
    if (!s_mctx) {
        winalp_log(WINALP_LOG_ERROR, "VLM: mtmd_init_from_file failed");
        return false;
    }

    if (!mtmd_support_vision(s_mctx))
        winalp_log(WINALP_LOG_WARN, "VLM: mmproj does not support vision");

    s_ready = true;
    winalp_log(WINALP_LOG_INFO, "VLM: engine ready (mmproj=%s)", mmproj_path);
    return true;
}

void vlm_engine_shutdown(void) {
    if (s_mctx) {
        mtmd_free(s_mctx);
        s_mctx = NULL;
    }
    s_ready = false;
}

bool vlm_engine_is_ready(void) {
    return s_ready && s_mctx != NULL;
}

void vlm_engine_bgra_to_rgb(const unsigned char *bgra, int w, int h,
                            unsigned char *rgb_out) {
    int n = w * h;
    for (int i = 0; i < n; i++) {
        rgb_out[i * 3 + 0] = bgra[i * 4 + 2]; /* B → R */
        rgb_out[i * 3 + 1] = bgra[i * 4 + 1]; /* G → G */
        rgb_out[i * 3 + 2] = bgra[i * 4 + 0]; /* R → B */
    }
}

bool vlm_engine_process(const char *text_prompt,
                         const unsigned char *pixels_rgb, int width, int height,
                         VlmTokenCallback callback, void *userdata) {
    if (!s_mctx || !text_prompt || !pixels_rgb || width <= 0 || height <= 0 || !callback)
        return false;

    struct llama_context *lctx = ai_engine_get_llama_ctx();
    struct llama_model  *model = ai_engine_get_model();
    if (!lctx || !model) {
        winalp_log(WINALP_LOG_ERROR, "VLM: no llama context/model available");
        return false;
    }

    /* Build prompt with media marker */
    char prompt[4096];
    const char *marker = mtmd_get_marker(s_mctx);
    if (!marker) marker = mtmd_default_marker();
    int n_prompt = snprintf(prompt, sizeof(prompt), "%s %s", marker,
                            text_prompt ? text_prompt : "describe");
    if (n_prompt <= 0) return false;

    /* Create mtmd_bitmap from RGB pixels */
    mtmd_bitmap *bitmap = mtmd_bitmap_init((uint32_t)width, (uint32_t)height, pixels_rgb);
    if (!bitmap) {
        winalp_log(WINALP_LOG_ERROR, "VLM: mtmd_bitmap_init failed");
        return false;
    }

    /* Prepare text input */
    mtmd_input_text inp_text;
    inp_text.text = prompt;
    inp_text.add_special = true;
    inp_text.parse_special = false;

    /* Tokenize prompt + image */
    mtmd_input_chunks *chunks = mtmd_input_chunks_init();
    const mtmd_bitmap *bitmaps[1] = { bitmap };
    int32_t ret = mtmd_tokenize(s_mctx, chunks, &inp_text, bitmaps, 1);
    if (ret != 0) {
        winalp_log(WINALP_LOG_ERROR, "VLM: mtmd_tokenize failed (code=%d)", ret);
        mtmd_bitmap_free(bitmap);
        mtmd_input_chunks_free(chunks);
        return false;
    }
    mtmd_bitmap_free(bitmap);

    /* Evaluate chunks — encodes image + runs text prefill */
    llama_pos n_past = 0;
    const int32_t n_batch = 512;
    ret = mtmd_helper_eval_chunks(s_mctx, lctx, chunks, n_past, 0, n_batch,
                                   true, &n_past);
    mtmd_input_chunks_free(chunks);
    if (ret != 0) {
        winalp_log(WINALP_LOG_ERROR, "VLM: mtmd_helper_eval_chunks failed");
        return false;
    }

    /* Generation loop */
    struct llama_sampler *smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    if (!smpl) return false;
    llama_sampler_chain_add(smpl, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.7f));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    const struct llama_vocab *vocab = llama_model_get_vocab(model);
    int eos = llama_vocab_eos(vocab);
    int max_gen = 512;
    char piece[64];

    for (int i = 0; i < max_gen; i++) {
        llama_sampler_reset(smpl);
        llama_token token = llama_sampler_sample(smpl, lctx, -1);
        if (token == eos) break;

        int n_chars = llama_token_to_piece(vocab, token, piece,
                                           (int)sizeof(piece), 0, false);
        if (n_chars > 0) {
            if (n_chars >= (int)sizeof(piece))
                n_chars = (int)sizeof(piece) - 1;
            piece[n_chars] = '\0';
            callback(piece, userdata);
        }

        struct llama_batch next = llama_batch_get_one(&token, 1);
        if (llama_decode(lctx, next) != 0) break;
    }

    llama_sampler_free(smpl);
    return true;
}
