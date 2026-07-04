#ifdef __ANDROID__

#include "platform/android_ui.h"

#include <jni.h>
#include <SDL3/SDL_system.h>

namespace
{
struct ActivityRef
{
    JNIEnv* env;
    jobject activity;
    jclass cls;
    ActivityRef()
    {
        env = (JNIEnv*)SDL_GetAndroidJNIEnv();
        activity = (jobject)SDL_GetAndroidActivity();
        cls = env->GetObjectClass(activity);
    }
};

int callIntMethod(const char* name)
{
    ActivityRef a;
    jmethodID id = a.env->GetMethodID(a.cls, name, "()I");
    return (int)a.env->CallIntMethod(a.activity, id);
}
} // namespace

float androidGetDpi()
{
    ActivityRef a;
    jmethodID id = a.env->GetMethodID(a.cls, "getDpi", "()F");
    return (float)a.env->CallFloatMethod(a.activity, id);
}

int androidStatusBarHeight() { return callIntMethod("getStatusBarHeight"); }
int androidNavigationBarHeight() { return callIntMethod("getNavigationBarHeight"); }

#endif // __ANDROID__
