#include "../include/pdf_reader.h"
#include "../include/logger.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Minimal PDF text extractor — handles simple uncompressed PDFs */
char *pdf_reader_extract_text(const char *pdf_path) {
    if (!pdf_path) return NULL;

    FILE *f = fopen(pdf_path, "rb");
    if (!f) {
        winalp_log(WINALP_LOG_WARN, "pdf_reader: cannot open %s", pdf_path);
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

    /* Allocate result buffer (4x PDF size is generous upper bound) */
    size_t cap = (size_t)sz * 4 + 1;
    if (cap < 4096) cap = 4096;
    char *result = (char*)calloc(cap, 1);
    if (!result) { free(buf); return NULL; }
    size_t pos = 0;

    /* Extract text between parentheses in BT...ET blocks */
    char *p = buf;
    while ((p = strstr(p, "BT"))) {
        /* Find matching ET */
        char *block_end = strstr(p, "ET");
        if (!block_end) break;

        /* Scan inside the block for (text) patterns */
        char *scan = p + 2;
        while (scan < block_end) {
            if (*scan == '(') {
                scan++; /* skip opening paren */
                while (scan < block_end && *scan != ')' && pos < cap - 1) {
                    if (*scan == '\\') {
                        scan++;
                        if (*scan == 'n') { result[pos++] = '\n'; scan++; continue; }
                        if (*scan == 'r') { result[pos++] = '\r'; scan++; continue; }
                        if (*scan == 't') { result[pos++] = '\t'; scan++; continue; }
                        if (!*scan) break;
                    }
                    if (*scan >= 32 && *scan < 127)
                        result[pos++] = *scan;
                    scan++;
                }
                if (pos < cap - 1) result[pos++] = ' ';
            } else {
                scan++;
            }
        }
        p = block_end + 2;
    }

    /* Trim trailing space */
    while (pos > 0 && result[pos - 1] == ' ') result[--pos] = '\0';

    free(buf);

    if (pos == 0) {
        free(result);
        winalp_log(WINALP_LOG_WARN, "pdf_reader: no extractable text in %s", pdf_path);
        return NULL;
    }

    winalp_log(WINALP_LOG_INFO, "pdf_reader: extracted %zu bytes from %s", pos, pdf_path);
    return result;
}

/* pdf_reader_render_page is implemented in pdf_render_winrt.cpp (C++ WinRT COM) */
