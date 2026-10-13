#include "clip46_runtime.h"
#include "config_store.h"
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

int main(void)
{
    const char *path = "clip46_runtime_test.tmp";
    FILE *fp;
    struct clip46_runtime cfg;
    struct clip46_poc_config secrets;
    struct clip46_uds_config uds;
    unsigned char seed[32], sendkey[32];
    static const unsigned char expected[32] = {
        0x95,0x4f,0x64,0xf2,0xe4,0xe8,0x6e,0x9e,
        0xee,0x82,0xd2,0x02,0x16,0x68,0x48,0x99,
        0x5d,0x72,0xd0,0x07,0xa9,0x2a,0x20,0x38,
        0x7e,0xbd,0xfa,0x04,0x24,0xf0,0x44,0x96
    };
    unsigned int i;
#ifndef _WIN32
    struct stat info;
#endif
    fp = fopen(path, "wb");
    if (fp == NULL) return 1;
    fputs("[experimental]\n"
          "gtis46_secure_read = true\n"
          "gtis46_da00_plan = true\n"
          "gtis46_live_programming = true\n"
          "gtis46_conn_override = 11\n"
          "[gtis46-config]\n"
          "KEY_HEX = \"000102030405060708090A0B0C0D0E0F\"\n"
          "CONTEXT_HEX = \"000102030405060708090A0B0C0D0E0F"
          "101112131415161718191A1B1C1D1E1F"
          "202122232425262728292A2B2C2D2E2F303132\"\n"
          , fp);
    fputs("tool_version = \"000F\"\n"
          "tool_instance = \"10000006\"\n"
          "tool_family = \"010203040506\"\n"
          "include_optional = true\n"
          "optional_byte = \"02\"\n"
          "uds_tool_sa = \"F9\"\n"
          "clip_tool_sa = \"F1\"\n"
          "aes_key_hex = \"000102030405060708090A0B0C0D0E0F\"\n",
          fp);
    fclose(fp);
    if (ct_config_load_path(path) != 0 ||
        clip46_runtime_load(&cfg) != 0 ||
        !cfg.secure_read || !cfg.da00_plan || !cfg.live_programming ||
        cfg.conn_override != 11U ||
        clip46_runtime_load_secrets(&secrets) != 0 ||
        secrets.key[0] != 0U || secrets.key[15] != 15U ||
        secrets.context[50] != 50U)
        return 2;
    if (clip46_runtime_load_uds(&uds) != 0 ||
        uds.tool_version != 15U ||
        uds.tool_instance != 0x10000006UL ||
        uds.include_optional != 1 ||
        uds.optional_byte != 2U ||
        uds.uds_tool_sa != 0xF9U ||
        uds.clip_tool_sa != 0xF1U ||
        uds.tool_family[5] != 6U)
        return 3;
    for (i = 0U; i < 32U; ++i)
        seed[i] = (unsigned char)i;
    if (clip46_runtime_build_uds_sendkey(seed, sendkey) != 0 ||
        memcmp(sendkey, expected, sizeof(expected)) != 0)
        return 4;
    clip46_poc_wipe(&uds, sizeof(uds));
#ifndef _WIN32
    /* Secret-bearing files must be saved with owner-only permissions. */
    if (ct_config_save() != 0 ||
        stat(path, &info) != 0 ||
        (info.st_mode & 0077) != 0)
        return 5;
#endif
    ct_config_close();
    remove(path);
    puts("GTIS4.6 TOML configuration test passed");
    return 0;
}
