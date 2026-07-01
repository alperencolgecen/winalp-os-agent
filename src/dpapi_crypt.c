#include "../include/dpapi_crypt.h"
#include <wincrypt.h>

bool dpapi_encrypt(const char *plaintext, size_t plain_len,
                   BYTE **out_blob, DWORD *out_len) {
    DATA_BLOB in  = { (DWORD)plain_len, (BYTE*)plaintext };
    DATA_BLOB out = { 0, NULL };

    if (!CryptProtectData(&in, NULL, NULL, NULL, NULL, 0, &out))
        return false;

    *out_blob = out.pbData;
    *out_len  = out.cbData;
    return true;
}

bool dpapi_decrypt(const BYTE *blob, DWORD blob_len,
                   char **out_text, DWORD *out_len) {
    DATA_BLOB in  = { blob_len, (BYTE*)blob };
    DATA_BLOB out = { 0, NULL };

    if (!CryptUnprotectData(&in, NULL, NULL, NULL, NULL, 0, &out))
        return false;

    *out_text = (char*)out.pbData;
    *out_len  = out.cbData;
    return true;
}

void dpapi_free(BYTE *p) {
    if (p) LocalFree(p);
}
