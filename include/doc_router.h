/* doc_router.h — Hybrid VLM/OCR document processing router */
#ifndef DOC_ROUTER_H
#define DOC_ROUTER_H
#include <stdbool.h>

/* Initialize the router — scans models/ for mmproj-*.gguf */
bool doc_router_init(const char *models_dir);

/* Check if a VLM (Vision Language Model) is available */
bool doc_router_is_vlm_available(void);

/* Process a screenshot bitmap (BGRA 32bpp) through the best available pipeline */
/*  bitmap   — raw pixel data
 *  width    — in pixels
 *  height   — in pixels
 *  out_text — recognized/described text (caller must free)
 *  returns  true if text was extracted
 */
bool doc_router_process_image(const unsigned char *bitmap, int width, int height,
                               char **out_text);

/* Process a PDF page through the best available pipeline */
/*  pdf_path  — path to the PDF file
 *  page_num  — zero-based page index
 *  out_text  — extracted text (caller must free)
 *  returns   true if text was extracted
 */
bool doc_router_process_pdf_page(const char *pdf_path, int page_num, char **out_text);

/* Shutdown */
void doc_router_shutdown(void);

#endif /* DOC_ROUTER_H */
