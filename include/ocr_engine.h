/* ocr_engine.h — Windows built-in OCR wrapper (WinRT COM) */
#ifndef OCR_ENGINE_H
#define OCR_ENGINE_H
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize OCR subsystem (call once at startup) */
bool ocr_engine_init(void);

/* Recognize text from a raw BGRA bitmap buffer */
/*  bitmap   — pointer to pixel data (BGRA, 32bpp)
 *  width    — bitmap width in pixels
 *  height   — bitmap height in pixels
 *  out_text — receives recognized text (caller must free with ocr_engine_free_text)
 *  returns  true if text was recognized
 */
bool ocr_engine_recognize(const unsigned char *bitmap, int width, int height,
                           char **out_text);

/* Free text returned by ocr_engine_recognize */
void ocr_engine_free_text(char *text);

/* Shutdown OCR subsystem */
void ocr_engine_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* OCR_ENGINE_H */
