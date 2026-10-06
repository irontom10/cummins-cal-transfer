/*
 * native_bridge.c
 *
 * Thin JNI boundary around the existing C89 calibration-transfer core.
 */

#include <jni.h>
#include <string.h>

#include "rp1210_android.h"
#include "rp1210clip.h"

static JavaVM *g_vm = NULL;
static jobject g_progress_target = NULL;
static jmethodID g_progress_method = NULL;

static void
clear_progress_target(JNIEnv *env)
{
    if (g_progress_target != NULL) {
        (*env)->DeleteGlobalRef(env, g_progress_target);
        g_progress_target = NULL;
    }
    g_progress_method = NULL;
}

static int
set_progress_target(JNIEnv *env, jobject target)
{
    jclass cls;

    clear_progress_target(env);

    if (target == NULL)
        return 1;

    g_progress_target = (*env)->NewGlobalRef(env, target);
    if (g_progress_target == NULL)
        return 0;

    cls = (*env)->GetObjectClass(env, target);
    if (cls == NULL) {
        clear_progress_target(env);
        return 0;
    }

    g_progress_method = (*env)->GetMethodID(
        env,
        cls,
        "onNativeProgress",
        "(ILjava/lang/String;)V");

    (*env)->DeleteLocalRef(env, cls);

    if (g_progress_method == NULL) {
        clear_progress_target(env);
        return 0;
    }

    return 1;
}

static void
jni_progress(int percent, const char *message)
{
    JNIEnv *env;
    jint env_rc;
    int attached;
    jstring text;

    if (g_vm == NULL ||
        g_progress_target == NULL ||
        g_progress_method == NULL) {
        return;
    }

    env = NULL;
    attached = 0;

    env_rc = (*g_vm)->GetEnv(
        g_vm,
        (void **)&env,
        JNI_VERSION_1_6);

    if (env_rc == JNI_EDETACHED) {
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK)
            return;
        attached = 1;
    } else if (env_rc != JNI_OK) {
        return;
    }

    text = (*env)->NewStringUTF(
        env,
        message != NULL ? message : "");

    if (text != NULL) {
        (*env)->CallVoidMethod(
            env,
            g_progress_target,
            g_progress_method,
            (jint)percent,
            text);
        (*env)->DeleteLocalRef(env, text);
    }

    if (attached)
        (*g_vm)->DetachCurrentThread(g_vm);
}

JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)reserved;
    g_vm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT jint JNICALL
Java_com_irontom10_calibrationtransfer_NativeBridge_configureRp1210(
    JNIEnv *env,
    jobject self,
    jstring data_path,
    jstring mac_address)
{
    const char *path;
    const char *mac;
    int ok;

    (void)self;

    if (data_path == NULL || mac_address == NULL)
        return 0;

    path = (*env)->GetStringUTFChars(env, data_path, NULL);
    mac = (*env)->GetStringUTFChars(env, mac_address, NULL);

    if (path == NULL || mac == NULL) {
        if (path != NULL)
            (*env)->ReleaseStringUTFChars(env, data_path, path);
        if (mac != NULL)
            (*env)->ReleaseStringUTFChars(env, mac_address, mac);
        return 0;
    }

    ok = rp1210_android_configure(path, mac);

    (*env)->ReleaseStringUTFChars(env, data_path, path);
    (*env)->ReleaseStringUTFChars(env, mac_address, mac);

    return ok ? 1 : 0;
}

static jint
run_transfer(JNIEnv *env,
             jstring api_name,
             jint baud,
             jint tool_sa,
             jint ecm_sa,
             jstring path_string,
             jobject progress_target,
             int upload)
{
    const char *api;
    const char *path;
    int rc;

    if (api_name == NULL || path_string == NULL)
        return -100;

    api = (*env)->GetStringUTFChars(env, api_name, NULL);
    path = (*env)->GetStringUTFChars(env, path_string, NULL);

    if (api == NULL || path == NULL) {
        if (api != NULL)
            (*env)->ReleaseStringUTFChars(env, api_name, api);
        if (path != NULL)
            (*env)->ReleaseStringUTFChars(env, path_string, path);
        return -100;
    }

    if (!set_progress_target(env, progress_target)) {
        (*env)->ReleaseStringUTFChars(env, api_name, api);
        (*env)->ReleaseStringUTFChars(env, path_string, path);
        return -109;
    }

    if (upload) {
        rc = rp1210_upload_ccal(
            api,
            2,
            (int)baud,
            (unsigned char)tool_sa,
            (unsigned char)ecm_sa,
            path,
            jni_progress);
    } else {
        rc = rp1210_pull_ccal(
            api,
            2,
            (int)baud,
            (unsigned char)tool_sa,
            (unsigned char)ecm_sa,
            path,
            jni_progress);
    }

    clear_progress_target(env);

    (*env)->ReleaseStringUTFChars(env, api_name, api);
    (*env)->ReleaseStringUTFChars(env, path_string, path);

    return (jint)rc;
}

JNIEXPORT jint JNICALL
Java_com_irontom10_calibrationtransfer_NativeBridge_pullCcal(
    JNIEnv *env,
    jobject self,
    jstring api_name,
    jint baud,
    jint tool_sa,
    jint ecm_sa,
    jstring path,
    jobject progress_target)
{
    (void)self;
    return run_transfer(
        env,
        api_name,
        baud,
        tool_sa,
        ecm_sa,
        path,
        progress_target,
        0);
}

JNIEXPORT jint JNICALL
Java_com_irontom10_calibrationtransfer_NativeBridge_uploadCcal(
    JNIEnv *env,
    jobject self,
    jstring api_name,
    jint baud,
    jint tool_sa,
    jint ecm_sa,
    jstring path,
    jobject progress_target)
{
    (void)self;
    return run_transfer(
        env,
        api_name,
        baud,
        tool_sa,
        ecm_sa,
        path,
        progress_target,
        1);
}

JNIEXPORT jstring JNICALL
Java_com_irontom10_calibrationtransfer_NativeBridge_getLastError(
    JNIEnv *env,
    jobject self)
{
    char buffer[512];

    (void)self;

    buffer[0] = '\0';
    rp1210_get_last_error(buffer, (int)sizeof(buffer));
    return (*env)->NewStringUTF(env, buffer);
}
