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

#endif
