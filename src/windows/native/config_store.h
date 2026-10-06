#ifndef CT_CONFIG_STORE_H
#define CT_CONFIG_STORE_H

#ifdef _WIN32
#define CT_CONFIG_CALL __cdecl
#ifdef CT_CONFIG_EXPORTS
#define CT_CONFIG_API __declspec(dllexport)
#else
#define CT_CONFIG_API __declspec(dllimport)
#endif
#else
#define CT_CONFIG_CALL
#define CT_CONFIG_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Standalone TOML-backed UI configuration store.
 *
 * This module is intentionally independent of the RP1210/protocol core.
 * It owns only:
 *   ~/.config/CalibrationTransfer/config.toml
 *
 * The API is generic: sections and keys are supplied by the caller so future
 * UI settings do not require changes to this library.  Typed helpers cover the
 * common scalar TOML values and the raw helpers leave room for arrays/inline
 * tables or other TOML values later.
 */

CT_CONFIG_API int CT_CONFIG_CALL ct_config_load_default(void);
CT_CONFIG_API int CT_CONFIG_CALL ct_config_save(void);
CT_CONFIG_API void CT_CONFIG_CALL ct_config_close(void);

CT_CONFIG_API int CT_CONFIG_CALL ct_config_get_string(
    const char *section,
    const char *key,
    const char *default_value,
    char *out,
    int out_size);

CT_CONFIG_API int CT_CONFIG_CALL ct_config_get_int(
    const char *section,
    const char *key,
    long default_value,
    long *out_value);

CT_CONFIG_API int CT_CONFIG_CALL ct_config_get_bool(
    const char *section,
    const char *key,
    int default_value,
    int *out_value);

CT_CONFIG_API int CT_CONFIG_CALL ct_config_get_raw(
    const char *section,
    const char *key,
    char *out,
    int out_size);

CT_CONFIG_API int CT_CONFIG_CALL ct_config_set_string(
    const char *section,
    const char *key,
    const char *value);

CT_CONFIG_API int CT_CONFIG_CALL ct_config_set_int(
    const char *section,
    const char *key,
    long value);

CT_CONFIG_API int CT_CONFIG_CALL ct_config_set_bool(
    const char *section,
    const char *key,
    int value);

CT_CONFIG_API int CT_CONFIG_CALL ct_config_set_raw(
    const char *section,
    const char *key,
    const char *toml_value);

CT_CONFIG_API int CT_CONFIG_CALL ct_config_get_path(
    char *out,
    int out_size);

CT_CONFIG_API int CT_CONFIG_CALL ct_config_get_last_error(
    char *out,
    int out_size);

#ifdef __cplusplus
}
#endif

#endif /* CT_CONFIG_STORE_H */
