#include "openride/android_region_download.h"

#if defined(__ANDROID__)

#include <SDL3/SDL_system.h>
#include <jni.h>
#include <stdio.h>
#include <string.h>

static jobject activity_ref(JNIEnv **out_env)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (out_env) *out_env = env;
    if (!env || !activity) return NULL;
    return activity;
}

bool openride_android_region_download_start(const char *url,
                                            const char *relative_path)
{
    if (!url || !relative_path) return false;
    JNIEnv *env = NULL;
    jobject activity = activity_ref(&env);
    if (!activity) return false;
    jclass clazz = (*env)->GetObjectClass(env, activity);
    if (!clazz) { (*env)->DeleteLocalRef(env, activity); return false; }
    jmethodID method = (*env)->GetMethodID(env,
                                           clazz,
                                           "openRideStartDownload",
                                           "(Ljava/lang/String;Ljava/lang/String;)Z");
    bool result = false;
    if (method) {
        jstring jurl = (*env)->NewStringUTF(env, url);
        jstring jpath = (*env)->NewStringUTF(env, relative_path);
        if (jurl && jpath) {
            result = (*env)->CallBooleanMethod(env, activity, method, jurl, jpath) == JNI_TRUE;
        }
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            result = false;
        }
        if (jurl) (*env)->DeleteLocalRef(env, jurl);
        if (jpath) (*env)->DeleteLocalRef(env, jpath);
    }
    (*env)->DeleteLocalRef(env, clazz);
    (*env)->DeleteLocalRef(env, activity);
    return result;
}

void openride_android_region_download_cancel(void)
{
    JNIEnv *env = NULL;
    jobject activity = activity_ref(&env);
    if (!activity) return;
    jclass clazz = (*env)->GetObjectClass(env, activity);
    if (clazz) {
        jmethodID method = (*env)->GetMethodID(env, clazz, "openRideCancelDownload", "()V");
        if (method) (*env)->CallVoidMethod(env, activity, method);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, clazz);
    }
    (*env)->DeleteLocalRef(env, activity);
}

bool openride_android_region_download_poll(OpenRideAndroidDownloadStatus *status)
{
    if (!status) return false;
    memset(status, 0, sizeof(*status));
    JNIEnv *env = NULL;
    jobject activity = activity_ref(&env);
    if (!activity) return false;
    bool ok = false;
    jclass clazz = (*env)->GetObjectClass(env, activity);
    if (clazz) {
        jmethodID read = (*env)->GetMethodID(env, clazz, "openRideReadDownload", "()[J");
        if (read) {
            jlongArray array = (jlongArray)(*env)->CallObjectMethod(env, activity, read);
            if (!(*env)->ExceptionCheck(env) && array && (*env)->GetArrayLength(env, array) >= 3) {
                jlong values[3] = {0};
                (*env)->GetLongArrayRegion(env, array, 0, 3, values);
                status->state = (OpenRideAndroidDownloadState)values[0];
                status->bytes_downloaded = values[1] > 0 ? (uint64_t)values[1] : 0U;
                status->total_bytes = values[2] > 0 ? (uint64_t)values[2] : 0U;
                ok = true;
            }
            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
            if (array) (*env)->DeleteLocalRef(env, array);
        }
        if (ok && status->state == OPENRIDE_ANDROID_DOWNLOAD_ERROR) {
            jmethodID err = (*env)->GetMethodID(env,
                                                clazz,
                                                "openRideReadDownloadError",
                                                "()Ljava/lang/String;");
            if (err) {
                jstring text = (jstring)(*env)->CallObjectMethod(env, activity, err);
                if (!(*env)->ExceptionCheck(env) && text) {
                    const char *utf = (*env)->GetStringUTFChars(env, text, NULL);
                    if (utf) {
                        snprintf(status->error, sizeof(status->error), "%s", utf);
                        (*env)->ReleaseStringUTFChars(env, text, utf);
                    }
                }
                if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
                if (text) (*env)->DeleteLocalRef(env, text);
            }
        }
        (*env)->DeleteLocalRef(env, clazz);
    }
    (*env)->DeleteLocalRef(env, activity);
    return ok;
}

#else

bool openride_android_region_download_start(const char *url,
                                            const char *relative_path)
{
    (void)url; (void)relative_path; return false;
}
void openride_android_region_download_cancel(void) {}
bool openride_android_region_download_poll(OpenRideAndroidDownloadStatus *status)
{
    if (status) memset(status, 0, sizeof(*status));
    return false;
}

#endif
