#ifndef VLM_ENGINE_H
#define VLM_ENGINE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*VlmTokenCallback)(const char *token, void *userdata);

bool  vlm_engine_init(const char *mmproj_path);
void  vlm_engine_shutdown(void);
bool  vlm_engine_is_ready(void);

/* Process image + text prompt through VLM. pixels_rgb must be RGB24 (3 bytes/pixel).
   Internally calls mtmd_tokenize + mtmd_helper_eval_chunks + generation loop.
   callback is called for each generated token. */
bool  vlm_engine_process(const char *text_prompt,
                         const unsigned char *pixels_rgb, int width, int height,
                         VlmTokenCallback callback, void *userdata);

/* Convert BGRA 32bpp → RGB 24bpp in-place or to a separate buffer */
void  vlm_engine_bgra_to_rgb(const unsigned char *bgra, int w, int h,
                             unsigned char *rgb_out);

#ifdef __cplusplus
}
#endif

#endif /* VLM_ENGINE_H */
