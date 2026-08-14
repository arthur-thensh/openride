#include "openride/android_voice_guidance.h"

#if defined(__ANDROID__)

#include <SDL3/SDL_system.h>
#include <jni.h>

static jobject activity_ref(JNIEnv **out_env)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (out_env) *out_env = env;
    if (!env || !activity) return NULL;
    return activity;
}

static bool call_boolean_activity_method(const char *name)
{
    JNIEnv *env = NULL;
    jobject activity = activity_ref(&env);
    if (!activity || !name) return false;

    bool result = false;
    jclass clazz = (*env)->GetObjectClass(env, activity);
    if (clazz) {
        jmethodID method = (*env)->GetMethodID(env, clazz, name, "()Z");
        if (method) {
            result = (*env)->CallBooleanMethod(env, activity, method) == JNI_TRUE;
        }
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            result = false;
        }
        (*env)->DeleteLocalRef(env, clazz);
    }
    (*env)->DeleteLocalRef(env, activity);
    return result;
}

static void call_void_activity_method(const char *name)
{
    JNIEnv *env = NULL;
    jobject activity = activity_ref(&env);
    if (!activity || !name) return;

    jclass clazz = (*env)->GetObjectClass(env, activity);
    if (clazz) {
        jmethodID method = (*env)->GetMethodID(env, clazz, name, "()V");
        if (method) {
            (*env)->CallVoidMethod(env, activity, method);
        }
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, clazz);
    }
    (*env)->DeleteLocalRef(env, activity);
}

bool openride_android_voice_guidance_init(void)
{
    return call_boolean_activity_method("openRideTtsInit");
}

bool openride_android_voice_guidance_ready(void)
{
    return call_boolean_activity_method("openRideTtsReady");
}

bool openride_android_voice_guidance_speak(const char *text, bool flush)
{
    if (!text || !text[0]) return false;

    JNIEnv *env = NULL;
    jobject activity = activity_ref(&env);
    if (!activity) return false;

    bool result = false;
    jclass clazz = (*env)->GetObjectClass(env, activity);
    if (clazz) {
        jmethodID method = (*env)->GetMethodID(
            env,
            clazz,
            "openRideTtsSpeak",
            "(Ljava/lang/String;Z)Z");
        if (method) {
            jstring jtext = (*env)->NewStringUTF(env, text);
            if (jtext) {
                result = (*env)->CallBooleanMethod(env,
                                                    activity,
                                                    method,
                                                    jtext,
                                                    flush ? JNI_TRUE : JNI_FALSE)
                    == JNI_TRUE;
                (*env)->DeleteLocalRef(env, jtext);
            }
        }
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            result = false;
        }
        (*env)->DeleteLocalRef(env, clazz);
    }
    (*env)->DeleteLocalRef(env, activity);
    return result;
}

void openride_android_voice_guidance_stop(void)
{
    call_void_activity_method("openRideTtsStop");
}

void openride_android_voice_guidance_shutdown(void)
{
    call_void_activity_method("openRideTtsShutdown");
}

static bool backend_ready(void *userdata)
{
    (void)userdata;
    return openride_android_voice_guidance_ready();
}

static bool backend_speak(void *userdata, const char *text, bool flush)
{
    (void)userdata;
    return openride_android_voice_guidance_speak(text, flush);
}

static void backend_stop(void *userdata)
{
    (void)userdata;
    openride_android_voice_guidance_stop();
}

OpenRideVoiceGuidanceBackend openride_android_voice_guidance_backend(void)
{
    OpenRideVoiceGuidanceBackend backend = {0};
    backend.ready = backend_ready;
    backend.speak = backend_speak;
    backend.stop = backend_stop;
    return backend;
}

#else

bool openride_android_voice_guidance_init(void)
{
    return false;
}

bool openride_android_voice_guidance_ready(void)
{
    return false;
}

bool openride_android_voice_guidance_speak(const char *text, bool flush)
{
    (void)text;
    (void)flush;
    return false;
}

void openride_android_voice_guidance_stop(void)
{
}

void openride_android_voice_guidance_shutdown(void)
{
}

OpenRideVoiceGuidanceBackend openride_android_voice_guidance_backend(void)
{
    OpenRideVoiceGuidanceBackend backend = {0};
    return backend;
}

#endif
