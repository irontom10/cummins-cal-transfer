/*
 * GTIS4.6 options and secret loading from the existing C89 TOML store.
 * Deliberately no environment-variable override, credential logging or
 * implicit opt-in. Android's previously loaded private config is reused.
 */
#include "clip46_runtime.h"
/* Core embeds config_store.c in its native library on Windows. */
#define CT_CONFIG_EXPORTS
#include "config_store.h"
#include "da00_sendkey.h"
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

/* Strict fixed-width hex settings so no decimal / endian ambiguity. */
static int
get_hex_field(const char *name, unsigned char *value, size_t len)
{
    char source[130];
    int ok;
    memset(source, 0, sizeof(source));
    ok = ct_config_get_string("gtis46-config", name, "",
                              source, (int)sizeof(source)) &&
         clip46_poc_decode_hex(source, value, len) == 0;
    clip46_poc_wipe(source, sizeof(source));
    return ok ? 0 : -1;
}

int
clip46_runtime_load_uds(struct clip46_uds_config *cfg)
{
    unsigned char ver[2];
    unsigned char inst[4];
    unsigned char optional[1];
    unsigned char address[1];
    int ok;

    if (cfg == NULL)
        return -1;
    memset(cfg, 0, sizeof(*cfg));
    memset(ver, 0, sizeof(ver));
    memset(inst, 0, sizeof(inst));
    memset(optional, 0, sizeof(optional));
    memset(address, 0, sizeof(address));
    ok = ensure_loaded() == 0 &&
         get_hex_field("tool_version", ver, sizeof(ver)) == 0 &&
         get_hex_field("tool_instance", inst, sizeof(inst)) == 0 &&
         get_hex_field("tool_family", cfg->tool_family,
                       sizeof(cfg->tool_family)) == 0 &&
         get_hex_field("optional_byte", optional, sizeof(optional)) == 0 &&
         get_hex_field("uds_tool_sa", address, sizeof(address)) == 0 &&
         get_hex_field("aes_key_hex", cfg->aes_key,
                       sizeof(cfg->aes_key)) == 0 &&
         ct_config_get_bool("gtis46-config", "include_optional", 0,
                            &cfg->include_optional);
    if (ok) {
        cfg->tool_version = ((unsigned int)ver[0] << 8) |
                            (unsigned int)ver[1];
        cfg->tool_instance = ((unsigned long)inst[0] << 24) |
                             ((unsigned long)inst[1] << 16) |
                             ((unsigned long)inst[2] << 8) |
                             (unsigned long)inst[3];
        cfg->optional_byte = optional[0];
        cfg->uds_tool_sa = address[0];
    } else {
        clip46_poc_wipe(cfg, sizeof(*cfg));
    }
    clip46_poc_wipe(ver, sizeof(ver));
    clip46_poc_wipe(inst, sizeof(inst));
    clip46_poc_wipe(optional, sizeof(optional));
    clip46_poc_wipe(address, sizeof(address));
    return ok ? 0 : -1;
}

int
clip46_runtime_build_uds_sendkey(const unsigned char seed[32],
                                 unsigned char cryptogram[32])
{
    struct clip46_uds_config settings;
    int ok;
    if (seed == NULL || cryptogram == NULL)
        return -1;
    memset(&settings, 0, sizeof(settings));
    if (clip46_runtime_load_uds(&settings) != 0) {
        clip46_poc_wipe(cryptogram, 32U);
        return -1;
    }
    ok = da00_sendkey(seed, settings.aes_key,
                      settings.tool_version, settings.tool_instance,
                      settings.tool_family, settings.include_optional,
                      settings.optional_byte, cryptogram);
    clip46_poc_wipe(&settings, sizeof(settings));
    return ok ? 0 : -1;
}
