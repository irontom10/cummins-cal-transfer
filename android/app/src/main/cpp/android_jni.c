#include <jni.h>
#include <stddef.h>
#include <string.h>

#include "rp1210clip.h"

static JNIEnv *g_env;
static jobject g_transport;
static jobject g_progress;
static jmethodID g_open_method;
static jmethodID g_close_method;
static jmethodID g_send_method;
static jmethodID g_receive_method;
static jmethodID g_progress_method;

static int
jni_failed(JNIEnv *env)
{
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return 1;
    }
    return 0;
}

static int
bridge_begin(JNIEnv *env, jobject transport, jobject progress)
{
    jclass cls;

    if (env == NULL || transport == NULL)
        return 0;

    g_env = env;
    g_transport = transport;
    g_progress = progress;
    g_progress_method = NULL;

    cls = (*env)->GetObjectClass(env, transport);
    if (cls == NULL)
        return 0;

    g_open_method = (*env)->GetMethodID(env, cls, "open", "(I)Z");
    g_close_method = (*env)->GetMethodID(env, cls, "close", "()V");
    g_send_method = (*env)->GetMethodID(env, cls, "sendFrame", "(I[B)Z");
    g_receive_method = (*env)->GetMethodID(env, cls, "receiveFrame", "(I)[B");
    (*env)->DeleteLocalRef(env, cls);

    if (jni_failed(env) ||
        g_open_method == NULL ||
        g_close_method == NULL ||
        g_send_method == NULL ||
        g_receive_method == NULL) {
        return 0;
    }

    if (progress != NULL) {
        cls = (*env)->GetObjectClass(env, progress);
        if (cls == NULL)
            return 0;
        g_progress_method = (*env)->GetMethodID(
            env, cls, "onProgress", "(ILjava/lang/String;)V");
        (*env)->DeleteLocalRef(env, cls);
        if (jni_failed(env) || g_progress_method == NULL)
            return 0;
    }

    return 1;
}

static void
bridge_end(void)
{
    g_env = NULL;
    g_transport = NULL;
    g_progress = NULL;
    g_open_method = NULL;
    g_close_method = NULL;
    g_send_method = NULL;
    g_receive_method = NULL;
    g_progress_method = NULL;
}

int
android_can_open(int baud)
{
    jboolean ok;

    if (g_env == NULL || g_transport == NULL || g_open_method == NULL)
        return 0;

    ok = (*g_env)->CallBooleanMethod(
        g_env, g_transport, g_open_method, (jint)baud);
    if (jni_failed(g_env))
        return 0;

    return ok == JNI_TRUE;
}

void
android_can_close(void)
{
    if (g_env == NULL || g_transport == NULL || g_close_method == NULL)
        return;

    (*g_env)->CallVoidMethod(g_env, g_transport, g_close_method);
    (void)jni_failed(g_env);
}

int
android_can_send(unsigned long can_id,
                 const unsigned char *data,
                 unsigned int length)
{
    jbyteArray array;
    jboolean ok;

    if (g_env == NULL || g_transport == NULL ||
        g_send_method == NULL || data == NULL || length > 8U) {
        return 0;
    }

    array = (*g_env)->NewByteArray(g_env, (jsize)length);
    if (array == NULL)
        return 0;

    if (length != 0U) {
        (*g_env)->SetByteArrayRegion(
            g_env, array, 0, (jsize)length, (const jbyte *)data);
    }

    ok = (*g_env)->CallBooleanMethod(
        g_env,
        g_transport,
        g_send_method,
        (jint)(can_id & 0x1fffffffUL),
        array);

    (*g_env)->DeleteLocalRef(g_env, array);

    if (jni_failed(g_env))
        return 0;

    return ok == JNI_TRUE;
}

/* 1 = frame, 0 = timeout, -1 = Java/format error. */
int
android_can_receive(unsigned long *can_id,
                    unsigned char data[8],
                    unsigned int *length,
                    unsigned long timeout_ms)
{
    jbyteArray array;
    jsize n;
    jbyte raw[13];
    unsigned int dlc;

    if (g_env == NULL || g_transport == NULL ||
        g_receive_method == NULL ||
        can_id == NULL || data == NULL || length == NULL) {
        return -1;
    }

    if (timeout_ms > 2147483647UL)
        timeout_ms = 2147483647UL;

    array = (jbyteArray)(*g_env)->CallObjectMethod(
        g_env,
        g_transport,
        g_receive_method,
        (jint)timeout_ms);

    if (jni_failed(g_env))
        return -1;
    if (array == NULL)
        return 0;

    n = (*g_env)->GetArrayLength(g_env, array);
    if (n < 5 || n > (jsize)sizeof(raw)) {
        (*g_env)->DeleteLocalRef(g_env, array);
        return -1;
    }

    (*g_env)->GetByteArrayRegion(g_env, array, 0, n, raw);
    (*g_env)->DeleteLocalRef(g_env, array);
    if (jni_failed(g_env))
        return -1;

    dlc = (unsigned int)((unsigned char)raw[4]);
    if (dlc > 8U || n != (jsize)(5U + dlc))
        return -1;

    *can_id = ((unsigned long)(unsigned char)raw[0] << 24) |
              ((unsigned long)(unsigned char)raw[1] << 16) |
              ((unsigned long)(unsigned char)raw[2] << 8) |
              (unsigned long)(unsigned char)raw[3];
    *can_id &= 0x1fffffffUL;

    if (dlc != 0U)
        memcpy(data, raw + 5, dlc);
    *length = dlc;
    return 1;
}

static void
native_progress(int percent, const char *message)
{
    jstring text;

    if (g_env == NULL || g_progress == NULL || g_progress_method == NULL)
        return;

    text = (*g_env)->NewStringUTF(
        g_env, message != NULL ? message : "");
    if (text == NULL)
        return;

    (*g_env)->CallVoidMethod(
        g_env,
        g_progress,
        g_progress_method,
        (jint)percent,
        text);
    (*g_env)->DeleteLocalRef(g_env, text);
    (void)jni_failed(g_env);
}

JNIEXPORT jint JNICALL
Java_io_github_irontom10_caltransfer_NativeBridge_pull(
    JNIEnv *env,
    jclass clazz,
    jobject transport,
    jint baud,
    jint tool_sa,
    jint ecm_sa,
    jstring output_path,
    jobject progress)
{
    const char *path;
    int rc;

    (void)clazz;

    if (output_path == NULL ||
        tool_sa < 0 || tool_sa > 255 ||
        ecm_sa < 0 || ecm_sa > 255) {
        return -100;
    }

    if (!bridge_begin(env, transport, progress)) {
        bridge_end();
        return -103;
    }

    path = (*env)->GetStringUTFChars(env, output_path, NULL);
    if (path == NULL) {
        bridge_end();
        return -109;
    }

    rc = rp1210_pull_ccal(
        "android",
        0,
        (int)baud,
        (unsigned char)tool_sa,
        (unsigned char)ecm_sa,
        path,
        native_progress);

    (*env)->ReleaseStringUTFChars(env, output_path, path);
    bridge_end();
    return (jint)rc;
}

JNIEXPORT jint JNICALL
Java_io_github_irontom10_caltransfer_NativeBridge_upload(
    JNIEnv *env,
    jclass clazz,
    jobject transport,
    jint baud,
    jint tool_sa,
    jint ecm_sa,
    jstring ccal_path,
    jobject progress)
{
    const char *path;
    int rc;

    (void)clazz;

    if (ccal_path == NULL ||
        tool_sa < 0 || tool_sa > 255 ||
        ecm_sa < 0 || ecm_sa > 255) {
        return -100;
    }

    if (!bridge_begin(env, transport, progress)) {
        bridge_end();
        return -103;
    }

    path = (*env)->GetStringUTFChars(env, ccal_path, NULL);
    if (path == NULL) {
        bridge_end();
        return -109;
    }

    rc = rp1210_upload_ccal(
        "android",
        0,
        (int)baud,
        (unsigned char)tool_sa,
        (unsigned char)ecm_sa,
        path,
        native_progress);

    (*env)->ReleaseStringUTFChars(env, ccal_path, path);
    bridge_end();
    return (jint)rc;
}

JNIEXPORT jstring JNICALL
Java_io_github_irontom10_caltransfer_NativeBridge_lastError(
    JNIEnv *env,
    jclass clazz)
{
    char error[512];

    (void)clazz;
    error[0] = '\0';
    (void)rp1210_get_last_error(error, (int)sizeof(error));
    return (*env)->NewStringUTF(env, error);
}
