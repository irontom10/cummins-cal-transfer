#ifndef CLIP46_RUNTIME_H
#define CLIP46_RUNTIME_H

#include "clip46_poc_secure.h"

/* All experimental switches default to OFF. No credentials in source. */
struct clip46_runtime {
    int secure_read;
    int da00_plan;
    unsigned int conn_override;
};

/* Reads the already loaded TOML store, or loads the desktop default path. */
int clip46_runtime_load(struct clip46_runtime *cfg);
int clip46_runtime_load_secrets(struct clip46_poc_config *secrets);

/* UDS SecurityAccess settings use an INDEPENDENT key from CLIP 02 03.
 * Never print key contents or seed-derived cryptograms in logs. */
struct clip46_uds_config {
    unsigned int tool_version;
    unsigned long tool_instance;
    unsigned char tool_family[6];
    unsigned char optional_byte;
    unsigned char uds_tool_sa;
    int include_optional;
    unsigned char aes_key[16];
};

int clip46_runtime_load_uds(struct clip46_uds_config *cfg);

/* Offline pure calculation: takes a genuine 32-byte seed provided by a
 * caller. Does not request seeds or transmit any frame. */
int clip46_runtime_build_uds_sendkey(const unsigned char seed[32],
                                     unsigned char cryptogram[32]);

#endif
