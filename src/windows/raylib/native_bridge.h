#ifndef CT_RAYLIB_NATIVE_BRIDGE_H
#define CT_RAYLIB_NATIVE_BRIDGE_H

/* C89 ABI between the raylib frontend and the Windows RP1210 worker. */
#define CT_RAYLIB_PATH_CAP 1024
#define CT_RAYLIB_STATUS_CAP 512

struct ct_transfer_request {
    char api[64];
    char path[CT_RAYLIB_PATH_CAP];
    int device_id;
    int baud;
    unsigned char tool_sa;
    unsigned char ecm_sa;
    int upload;
};

struct ct_transfer_status {
    int percent;
    int done;
    int result;
    char status[CT_RAYLIB_STATUS_CAP];
    char error[CT_RAYLIB_STATUS_CAP];
};

void ct_native_init(void);
void ct_native_shutdown(void);

/* Returns 1 when chosen and 0 when cancelled. owner is a native HWND. */
int ct_native_choose_path(int upload, char *path, unsigned long cap,
                          void *owner);

/* Returns 0 on success. Job is copied and remains immutable until joined. */
int ct_native_begin(const struct ct_transfer_request *request);
void ct_native_snapshot(struct ct_transfer_status *snapshot);
void ct_native_join(void);

/* Implemented in raylib/main.c and called by the Windows entry point. */
int ct_raylib_main(void);

#endif
