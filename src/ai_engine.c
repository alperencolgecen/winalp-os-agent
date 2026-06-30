#include "../include/ai_engine.h"
#include "../include/llama.h"
#include "../include/logger.h"
#include <string.h>
#include <stdlib.h>

static struct llama_model   *s_model = NULL;
static struct llama_context *s_ctx   = NULL;
static struct llama_sampler *s_smpl  = NULL;
static int s_n_ctx = 0;

bool ai_engine_load(const char *model_path, int n_gpu_layers) {
    if (s_ctx) ai_engine_unload();

    struct llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = n_gpu_layers;

    s_model = llama_model_load_from_file(model_path, mparams);
    if (!s_model) {
        winalp_log(WINALP_LOG_ERROR, "AI: failed to load model: %s", model_path);
        return false;
    }

    char desc[128] = {0};
    llama_model_desc(s_model, desc, sizeof(desc));
    winalp_log(WINALP_LOG_INFO, "AI: model loaded: %s", desc);

    struct llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx            = 4096;
    cparams.n_threads        = 4;
    cparams.n_threads_batch  = 4;
    cparams.no_perf          = false;

    s_ctx = llama_init_from_model(s_model, cparams);
    if (!s_ctx) {
        winalp_log(WINALP_LOG_ERROR, "AI: failed to create context");
        llama_model_free(s_model);
        s_model = NULL;
        return false;
    }
    s_n_ctx = (int)cparams.n_ctx;

    s_smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(s_smpl, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(s_smpl, llama_sampler_init_temp(0.7f));
    llama_sampler_chain_add(s_smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    winalp_log(WINALP_LOG_INFO, "AI: engine ready (ctx=%d)", s_n_ctx);
    return true;
}

void ai_engine_infer(const char *prompt, TokenCallback cb, void *userdata) {
    if (!s_ctx || !cb) return;

    const struct llama_vocab *vocab = llama_model_get_vocab(s_model);

    int n_tokens = llama_tokenize(vocab, prompt, (int)strlen(prompt), NULL, 0, true, false);
    if (n_tokens <= 0) return;

    llama_token *tokens = (llama_token*)malloc((size_t)n_tokens * sizeof(llama_token));
    if (!tokens) return;

    llama_tokenize(vocab, prompt, (int)strlen(prompt), tokens, n_tokens, true, false);

    struct llama_batch batch = llama_batch_get_one(tokens, n_tokens);
    if (llama_decode(s_ctx, batch) != 0) {
        winalp_log(WINALP_LOG_ERROR, "AI: llama_decode failed on prompt");
        free(tokens);
        return;
    }

    llama_token eos_id = llama_vocab_eos(vocab);
    int n_gen = 0;
    const int max_gen = 512;
    char piece[32];

    while (n_gen < max_gen) {
        llama_sampler_reset(s_smpl);
        llama_token new_id = llama_sampler_sample(s_smpl, s_ctx, -1);

        if (new_id == eos_id) break;

        int n_chars = llama_token_to_piece(vocab, new_id, piece,
                                           (int)sizeof(piece), 0, false);
        if (n_chars > 0) {
            if (n_chars >= (int)sizeof(piece))
                n_chars = (int)sizeof(piece) - 1;
            piece[n_chars] = '\0';
            cb(piece, userdata);
        }

        struct llama_batch next = llama_batch_get_one(&new_id, 1);
        if (llama_decode(s_ctx, next) != 0) break;
        n_gen++;
    }

    free(tokens);
}

void ai_engine_reset_context(void) {
    if (!s_ctx || !s_model) return;

    struct llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx            = (uint32_t)s_n_ctx;
    cparams.n_threads        = 4;
    cparams.n_threads_batch  = 4;
    cparams.no_perf          = false;

    llama_free(s_ctx);
    s_ctx = llama_init_from_model(s_model, cparams);
    if (!s_ctx)
        winalp_log(WINALP_LOG_ERROR, "AI: failed to recreate context after reset");
    winalp_log(WINALP_LOG_INFO, "AI: context reset");
}

void ai_engine_unload(void) {
    if (s_smpl)  { llama_sampler_free(s_smpl); s_smpl = NULL; }
    if (s_ctx)   { llama_free(s_ctx);          s_ctx  = NULL; }
    if (s_model) { llama_model_free(s_model);  s_model = NULL; }
    winalp_log(WINALP_LOG_INFO, "AI: engine unloaded");
}

float ai_engine_tokens_per_sec(void) {
    if (!s_ctx) return 0.0f;
    struct llama_perf_context_data perf = llama_perf_context(s_ctx);
    if (perf.t_eval_ms < 0.001) return 0.0f;
    return (float)((double)perf.n_eval / (perf.t_eval_ms / 1000.0));
}
