/* pdf_reader.h — Static C PDF text / image extractor */
#ifndef PDF_READER_H
#define PDF_READER_H
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Extract text from a PDF file. Returns allocated string (caller frees). */
char *pdf_reader_extract_text(const char *pdf_path);

/* Extract page as raw pixel buffer for OCR or VLM (caller frees pixels). */
unsigned char *pdf_reader_render_page(const char *pdf_path, int page,
                                       int *out_w, int *out_h);

#ifdef __cplusplus
}
#endif

#endif /* PDF_READER_H */
