/* ISO C89 standalone UDS 0x27 08 SendKey payload constructor.
 * Requires the existing portable AES-128-CBC + PKCS#7 implementation.
 * No secret constants, firmware operations, or CAN I/O.
 */
#include "da00_sendkey.h"
#include "clip46_poc_secure.h"
#include <string.h>

int da00_sendkey(const unsigned char seed[32],
                 const unsigned char aes_key[16],
                 unsigned int tool_version,
                 unsigned long tool_instance,
                 const unsigned char tool_family[6],
                 int include_optional,
                 unsigned char optional_byte,
                 unsigned char out[32])
{
    unsigned char plaintext[29];
    size_t encrypted_len;
    int plaintext_len;
    int rc;
    if (seed == NULL || aes_key == NULL || tool_family == NULL ||
        out == NULL || tool_version > 65535U ||
        (include_optional != 0 && include_optional != 1))
        return 0;
    memset(plaintext, 0, sizeof(plaintext));
    memcpy(plaintext, seed + 16, 16U);
    plaintext[16] = (unsigned char)(tool_version >> 8);
    plaintext[17] = (unsigned char)(tool_version);
    plaintext[18] = (unsigned char)(tool_instance >> 24);
    plaintext[19] = (unsigned char)(tool_instance >> 16);
    plaintext[20] = (unsigned char)(tool_instance >> 8);
    plaintext[21] = (unsigned char)(tool_instance);
    memcpy(plaintext + 22, tool_family, 6U);
    plaintext_len = include_optional ? 29 : 28;
    if (include_optional)
        plaintext[28] = optional_byte;
    encrypted_len = 0U;
    rc = clip46_poc_encrypt_application(aes_key, seed, plaintext,
                                        (size_t)plaintext_len, out,
                                        32U, &encrypted_len);
    clip46_poc_wipe(plaintext, sizeof(plaintext));
    if (rc != 0 || encrypted_len != 32U) {
        clip46_poc_wipe(out, 32U);
        return 0;
    }
    return 1;
}
