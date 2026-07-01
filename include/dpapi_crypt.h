#ifndef DPAPI_CRYPT_H
#define DPAPI_CRYPT_H

#include <stdbool.h>
#include <windows.h>

bool dpapi_encrypt(const char *plaintext, size_t plain_len,
                   BYTE **out_blob, DWORD *out_len);
bool dpapi_decrypt(const BYTE *blob, DWORD blob_len,
                   char **out_text, DWORD *out_len);
void dpapi_free(BYTE *p);

#endif
