#ifndef RP1210_ANDROID_H
#define RP1210_ANDROID_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Configure the Android-specific state needed before the generic RP1210
 * transport is opened.  The Java UI owns Bluetooth discovery/pairing; this
 * backend only needs the selected MAC and writable vendor data directory.
 */
int rp1210_android_configure(const char *data_path,
                             const char *mac_address);

#ifdef __cplusplus
}
#endif

#endif /* RP1210_ANDROID_H */
