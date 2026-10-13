#include "clip46_runtime.h"
#include "config_store.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *path = "clip46_runtime_test.tmp";
    FILE *fp;
    struct clip46_runtime cfg;
    struct clip46_poc_config secrets;
    fp = fopen(path, "wb");
    if (fp == NULL) return 1;
    fputs("[experimental]\n"
          "gtis46_secure_read = true\n"
          "gtis46_da00_plan = true\n"
          "gtis46_conn_override = 11\n"
          "[gtis46-config]\n"
          "KEY_HEX = \"000102030405060708090A0B0C0D0E0F\"\n"
          "CONTEXT_HEX = \"000102030405060708090A0B0C0D0E0F"
          "101112131415161718191A1B1C1D1E1F"
          "202122232425262728292A2B2C2D2E2F303132\"\n",
          fp);
    fclose(fp);
    if (ct_config_load_path(path) != 0 ||
        clip46_runtime_load(&cfg) != 0 ||
        !cfg.secure_read || !cfg.da00_plan ||
        cfg.conn_override != 11U ||
        clip46_runtime_load_secrets(&secrets) != 0 ||
        secrets.key[0] != 0U || secrets.key[15] != 15U ||
        secrets.context[50] != 50U)
        return 2;
    ct_config_close();
    remove(path);
    puts("GTIS4.6 TOML configuration test passed");
    return 0;
}
