#include "../include/ai_engine.h"
#include "../include/llama.h"
#include "../include/sys_diag.h"
#include "../include/logger.h"
#include <string.h>
#include <stdlib.h>
#include <windows.h>

static struct llama_model   *s_model = NULL;
static struct llama_context *s_ctx   = NULL;
static struct llama_sampler *s_smpl  = NULL;
static int s_n_ctx = 0;
static bool s_is_vlm = false; /* true if model has separate encoder (vision-language) */

/* System prompt (set once after model load) */
#define MAX_SYS_PROMPT 16384
static char s_system_prompt[MAX_SYS_PROMPT] = "";

/* Dynamic context (active window, screen OCR) — updated from main loop */
#define MAX_DYN_CTX 2048
static char s_dynamic_context[MAX_DYN_CTX] = "";

/* Sliding window conversation buffer */
#define MAX_HISTORY 64
#define MAX_HIST_LEN 4096
static char s_history[MAX_HISTORY][MAX_HIST_LEN];
static int  s_history_count;
static int  s_history_head;
static int  s_ctx_usage_pct; /* last measured KV cache usage % */

bool ai_engine_is_loaded(void) {
    return s_ctx != NULL && s_model != NULL;
}

void ai_engine_set_system_prompt(const char *prompt) {
    strncpy(s_system_prompt, prompt ? prompt : "", sizeof(s_system_prompt) - 1);
}

void ai_engine_set_dynamic_context(const char *ctx) {
    strncpy(s_dynamic_context, ctx ? ctx : "", sizeof(s_dynamic_context) - 1);
}

/* Sliding window: trim memory when >70% full */
static void trim_context(void) {
    if (!s_ctx) return;
    llama_memory_t mem = llama_get_memory(s_ctx);
    if (!mem) return;

    llama_pos p_max = llama_memory_seq_pos_max(mem, 0);
    if (p_max < 0) return;
    s_ctx_usage_pct = (int)((p_max * 100) / s_n_ctx);
    if (s_ctx_usage_pct < 70) return;

    winalp_log(WINALP_LOG_INFO, "AI: context %d%% full — trimming sliding window", s_ctx_usage_pct);

    /* Remove first half of positions */
    llama_pos trim_at = p_max / 2;
    llama_memory_seq_rm(mem, 0, 0, trim_at);
    llama_memory_seq_add(mem, 0, trim_at, -1, -trim_at);
    s_ctx_usage_pct = (int)((llama_memory_seq_pos_max(mem, 0) * 100) / s_n_ctx);
    winalp_log(WINALP_LOG_INFO, "AI: context trimmed to %d%%", s_ctx_usage_pct);
}

/* Push a message into the sliding window history */
void ai_engine_push_history(const char *role, const char *content) {
    int idx = (s_history_head + s_history_count) % MAX_HISTORY;
    snprintf(s_history[idx], MAX_HIST_LEN, "%s: %s", role ? role : "user", content ? content : "");
    if (s_history_count < MAX_HISTORY) s_history_count++;
    else s_history_head = (s_history_head + 1) % MAX_HISTORY;
}

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

    s_history_count = 0;
    s_history_head  = 0;
    s_ctx_usage_pct = 0;

    /* Check if this is a vision-language model (has separate encoder) */
    s_is_vlm = llama_model_has_encoder(s_model);
    if (s_is_vlm)
        winalp_log(WINALP_LOG_INFO, "AI: VLM model detected (encoder present)");

    winalp_log(WINALP_LOG_INFO, "AI: engine ready (ctx=%d)", s_n_ctx);
    return true;
}

/* Load model with auto-detected GPU offload based on free VRAM */
bool ai_engine_load_auto(const char *model_path) {
    if (!model_path) return false;

    /* Phase 1: load with 0 GPU layers to count total layers */
    struct llama_model_params probe = llama_model_default_params();
    probe.n_gpu_layers = 0;
    struct llama_model *tmp = llama_model_load_from_file(model_path, probe);
    if (!tmp) {
        winalp_log(WINALP_LOG_ERROR, "AI: failed to probe model: %s", model_path);
        return false;
    }
    int n_total = (int)llama_model_n_layer(tmp);
    winalp_log(WINALP_LOG_INFO, "AI: model has %d layers total", n_total);
    llama_model_free(tmp);

    /* Phase 2: detect system VRAM and calculate offload */
    SysDiag d;
    sys_diag_detect(&d);
    int gpu_layers = sys_diag_recommend_gpu_layers(n_total, d.vram_free_mb);
    winalp_log(WINALP_LOG_INFO, "AI: auto GPU layers = %d (VRAM free=%llu MB, total=%llu MB)",
               gpu_layers, d.vram_free_mb, d.vram_total_mb);

    /* Phase 3: load model with calculated layers */
    return ai_engine_load(model_path, gpu_layers);
}

void ai_engine_infer(const char *prompt, TokenCallback cb, void *userdata) {
    if (!s_ctx || !cb) return;

    /* Trim context if too full */
    trim_context();

    /* Push user message to history */
    ai_engine_push_history("user", prompt);

    /* Build composite prompt: system + history + user */
    char composite[32768] = "";
    int pos = 0;
    int rem = (int)sizeof(composite);
#define CP_APPEND(fmt, ...) do { \
    int n = snprintf(composite + pos, (size_t)rem, fmt, ##__VA_ARGS__); \
    if (n > 0 && n < rem) { pos += n; rem -= n; } \
} while(0)

    if (s_system_prompt[0])
        CP_APPEND("%s\n\n", s_system_prompt);

    if (s_dynamic_context[0])
        CP_APPEND("[Live Context]\n%s\n\n", s_dynamic_context);

    /* Replay history (except the just-pushed user message which is included after) */
    int n_hist = s_history_count - 1;
    if (n_hist > 0) {
        int idx = s_history_head;
        for (int i = 0; i < n_hist; i++) {
            CP_APPEND("%s\n", s_history[idx]);
            idx = (idx + 1) % MAX_HISTORY;
        }
    }

    CP_APPEND("User: %s\nAssistant: ", prompt ? prompt : "");
#undef CP_APPEND

    const struct llama_vocab *vocab = llama_model_get_vocab(s_model);

    int n_tokens = llama_tokenize(vocab, composite, pos, NULL, 0, true, false);
    if (n_tokens <= 0) return;

    llama_token *tokens = (llama_token*)malloc((size_t)n_tokens * sizeof(llama_token));
    if (!tokens) return;

    llama_tokenize(vocab, composite, pos, tokens, n_tokens, true, false);

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
    char response[4096] = "";
    int resp_len = 0;

    while (n_gen < max_gen) {
        Sleep(0); /* yield to UI thread */
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

            /* Also accumulate for history */
            if (resp_len + n_chars < (int)sizeof(response) - 1) {
                memcpy(response + resp_len, piece, (size_t)n_chars);
                resp_len += n_chars;
                response[resp_len] = '\0';
            }
        }

        struct llama_batch next = llama_batch_get_one(&new_id, 1);
        if (llama_decode(s_ctx, next) != 0) break;
        n_gen++;
    }

    /* Push assistant response to history */
    if (resp_len > 0)
        ai_engine_push_history("assistant", response);

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
    else
        s_ctx_usage_pct = 0;
    s_history_count = 0;
    s_history_head  = 0;
    winalp_log(WINALP_LOG_INFO, "AI: context reset");
}

void ai_engine_unload(void) {
    if (s_smpl)  { llama_sampler_free(s_smpl); s_smpl = NULL; }
    if (s_ctx)   { llama_free(s_ctx);          s_ctx  = NULL; }
    if (s_model) { llama_model_free(s_model);  s_model = NULL; }
    s_history_count = 0;
    s_history_head  = 0;
    winalp_log(WINALP_LOG_INFO, "AI: engine unloaded");
}

bool ai_engine_is_vlm(void) {
    return s_is_vlm;
}

float ai_engine_tokens_per_sec(void) {
    if (!s_ctx) return 0.0f;
    struct llama_perf_context_data perf = llama_perf_context(s_ctx);
    if (perf.t_eval_ms < 0.001) return 0.0f;
    return (float)((double)perf.n_eval / (perf.t_eval_ms / 1000.0));
}

int ai_engine_context_usage(void) {
    return s_ctx_usage_pct;
}

struct llama_context * ai_engine_get_llama_ctx(void) {
    return s_ctx;
}

struct llama_model * ai_engine_get_model(void) {
    return s_model;
}
