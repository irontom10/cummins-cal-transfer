/*
 * GTIS4.6 options and secret loading from the existing C89 TOML store.
 * Deliberately no environment-variable override, credential logging or
 * implicit opt-in. Android's previously loaded private config is reused.
 */
#include "clip46_runtime.h"
/* Core embeds config_store.c in its native library on Windows. */
#define CT_CONFIG_EXPORTS
#include "config_store.h"
#include <string.h>

static int
ensure_loaded(void)
{
    char path[4096];
    if (ct_config_get_path(path, (int)sizeof(path)))
        return 0;
    return ct_config_load_default();
}

int
clip46_runtime_load(struct clip46_runtime *cfg)
{
    long override;
    if (cfg == NULL)
        return -1;
    memset(cfg, 0, sizeof(*cfg));
    if (ensure_loaded() != 0)
        return -1;
    (void)ct_config_get_bool("experimental", "gtis46_secure_read",
                             0, &cfg->secure_read);
    (void)ct_config_get_bool("experimental", "gtis46_da00_plan",
                             0, &cfg->da00_plan);
    override = 0L;
    (void)ct_config_get_int("experimental", "gtis46_conn_override",
                            0L, &override);
    if (override < 0L || override > 255L)
        return -1;
    cfg->conn_override = (unsigned int)override;
    return 0;
}

int
clip46_runtime_load_secrets(struct clip46_poc_config *secrets)
{
    char key[64];
    char context[128];
    int ok;
    if (secrets == NULL)
        return -1;
    memset(secrets, 0, sizeof(*secrets));
    memset(key, 0, sizeof(key));
    memset(context, 0, sizeof(context));
    ok = ensure_loaded() == 0 &&
         ct_config_get_string("gtis46-config", "KEY_HEX",
                              "", key, (int)sizeof(key)) &&
         ct_config_get_string("gtis46-config", "CONTEXT_HEX",
                              "", context, (int)sizeof(context)) &&
         clip46_poc_decode_hex(key, secrets->key, 16U) == 0 &&
         clip46_poc_decode_hex(context, secrets->context, 51U) == 0;
    clip46_poc_wipe(key, sizeof(key));
    clip46_poc_wipe(context, sizeof(context));
    if (!ok)
        clip46_poc_wipe(secrets, sizeof(*secrets));
    return ok ? 0 : -1;
}
