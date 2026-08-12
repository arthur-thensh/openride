#include "openride/android_location_provider.h"

#if defined(__ANDROID__)

#include <SDL3/SDL_system.h>
#include <jni.h>
#include <string.h>

static bool call_boolean_activity_method(const char *name)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!env || !activity || !name) return false;

    jclass clazz = (*env)->GetObjectClass(env, activity);
    if (!clazz) {
        (*env)->DeleteLocalRef(env, activity);
        return false;
    }

    jmethodID method = (*env)->GetMethodID(env, clazz, name, "()Z");
    bool result = false;
    if (method) {
        result = (*env)->CallBooleanMethod(env, activity, method) == JNI_TRUE;
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            result = false;
        }
    }

    (*env)->DeleteLocalRef(env, clazz);
    (*env)->DeleteLocalRef(env, activity);
    return result;
}

static void call_void_activity_method(const char *name)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!env || !activity || !name) return;

    jclass clazz = (*env)->GetObjectClass(env, activity);
    if (clazz) {
        jmethodID method = (*env)->GetMethodID(env, clazz, name, "()V");
        if (method) {
            (*env)->CallVoidMethod(env, activity, method);
            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        }
        (*env)->DeleteLocalRef(env, clazz);
    }
    (*env)->DeleteLocalRef(env, activity);
}

static bool android_location_start(void *userdata)
{
    OpenRideAndroidLocationContext *context = userdata;
    if (!context) return false;
    const bool started = call_boolean_activity_method("openRideStartLocation");
    context->started = started;
    if (started) context->last_timestamp_s = 0.0;
    return started;
}

static void android_location_stop(void *userdata)
{
    OpenRideAndroidLocationContext *context = userdata;
    call_void_activity_method("openRideStopLocation");
    if (context) context->started = false;
}

static bool android_location_poll(void *userdata,
                                  double delta_seconds,
                                  OpenRideLocationSample *sample)
{
    (void)delta_seconds;
    OpenRideAndroidLocationContext *context = userdata;
    if (!context || !sample || !context->started) return false;

    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!env || !activity) return false;

    bool result = false;
    jclass clazz = (*env)->GetObjectClass(env, activity);
    if (clazz) {
        jmethodID method = (*env)->GetMethodID(env, clazz, "openRideReadLocation", "()[D");
        if (method) {
            jdoubleArray array = (jdoubleArray)(*env)->CallObjectMethod(env, activity, method);
            if ((*env)->ExceptionCheck(env)) {
                (*env)->ExceptionClear(env);
            } else if (array && (*env)->GetArrayLength(env, array) >= 6) {
                jdouble values[6] = {0};
                (*env)->GetDoubleArrayRegion(env, array, 0, 6, values);
                const double timestamp_s = values[5];
                if (timestamp_s > context->last_timestamp_s) {
                    memset(sample, 0, sizeof(*sample));
                    sample->valid = true;
                    sample->lat = values[0];
                    sample->lon = values[1];
                    sample->speed_mps = values[2] >= 0.0 ? values[2] : 0.0;
                    sample->heading_deg = values[3] >= 0.0 ? values[3] : 0.0;
                    sample->accuracy_m = values[4] >= 0.0 ? values[4] : 0.0;
                    context->last_timestamp_s = timestamp_s;
                    result = true;
                }
            }
            if (array) (*env)->DeleteLocalRef(env, array);
        }
        (*env)->DeleteLocalRef(env, clazz);
    }
    (*env)->DeleteLocalRef(env, activity);
    return result;
}

void openride_android_location_provider_init(OpenRideLocationProvider *provider,
                                             OpenRideAndroidLocationContext *context)
{
    if (!provider || !context) return;
    memset(context, 0, sizeof(*context));
    openride_location_provider_init(provider,
                                    context,
                                    android_location_start,
                                    android_location_stop,
                                    android_location_poll);
}

#else

void openride_android_location_provider_init(OpenRideLocationProvider *provider,
                                             OpenRideAndroidLocationContext *context)
{
    (void)provider;
    (void)context;
}

#endif
