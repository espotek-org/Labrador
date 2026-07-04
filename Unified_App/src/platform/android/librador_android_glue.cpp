// JNI/SDL-Android glue backing librador's host hooks. This code used to live
// inside usbcallhandler.cpp; it talks to MainActivity for dialogs, media
// scanning, SAF file streams and APK asset extraction.
#ifdef __ANDROID__

#include "platform/android_hooks.h"
#include "librador_platform.h"
#include "logging_internal.h"

#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>

#include <cstdio>
#include <cstring>

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

void call_void_method(const char* name)
{
    ActivityRef a;
    jmethodID id = a.env->GetMethodID(a.cls, name, "()V");
    a.env->CallVoidMethod(a.activity, id);
}

void android_request_firmware_flash()
{
    call_void_method("requestFirmwareFlash");
}

void android_confirm_firmware_flash()
{
    call_void_method("confirmFirmwareFlash");
}

void android_set_bootloader_mode_allowed(bool allowed)
{
    ActivityRef a;
    jfieldID field = a.env->GetFieldID(a.cls, "bootloader_mode_allowed", "Z");
    a.env->SetBooleanField(a.activity, field, allowed);
}

void android_daq_file_written(const char* filepath)
{
    ActivityRef a;
    jmethodID id = a.env->GetMethodID(a.cls, "scanFile", "(Ljava/lang/String;)V");
    jstring jfilename = a.env->NewStringUTF(filepath);
    a.env->CallVoidMethod(a.activity, id, jfilename);
}

// stdio-backed SDL_IOStream via the public SDL_OpenIO interface (SDL_IOFromFP
// is internal-only and not exported from the shared libSDL3).
struct FileStreamCtx
{
    FILE* fp;
};

Sint64 fileStreamSize(void*) { return -1; }
Sint64 fileStreamSeek(void* userdata, Sint64 offset, SDL_IOWhence whence)
{
    FILE* fp = static_cast<FileStreamCtx*>(userdata)->fp;
    int origin = (whence == SDL_IO_SEEK_SET) ? SEEK_SET
        : (whence == SDL_IO_SEEK_CUR)        ? SEEK_CUR
                                             : SEEK_END;
    if (fseek(fp, (long)offset, origin) != 0)
        return -1;
    return ftell(fp);
}
size_t fileStreamRead(void* userdata, void* ptr, size_t size, SDL_IOStatus*)
{
    return fread(ptr, 1, size, static_cast<FileStreamCtx*>(userdata)->fp);
}
size_t fileStreamWrite(void* userdata, const void* ptr, size_t size, SDL_IOStatus*)
{
    return fwrite(ptr, 1, size, static_cast<FileStreamCtx*>(userdata)->fp);
}
bool fileStreamClose(void* userdata)
{
    FileStreamCtx* ctx = static_cast<FileStreamCtx*>(userdata);
    bool ok = (fclose(ctx->fp) == 0);
    delete ctx;
    return ok;
}

SDL_IOStream* android_open_daq_stream(const char* filepath)
{
    ActivityRef a;
    jmethodID initFileID
        = a.env->GetMethodID(a.cls, "initFile", "(Ljava/lang/String;)Ljava/lang/String;");
    jstring jfilename = a.env->NewStringUTF(filepath);
    jstring juri = (jstring)a.env->CallObjectMethod(a.activity, initFileID, jfilename);
    a.env->DeleteLocalRef(jfilename);

    // The file:// uri is a valid input to ContentResolver.openFileDescriptor,
    // reached through SDLActivity.openFileDescriptor (static, inherited by
    // MainActivity, so a.cls resolves it).
    jmethodID openFdID = a.env->GetStaticMethodID(
        a.cls, "openFileDescriptor", "(Ljava/lang/String;Ljava/lang/String;)I");
    jstring jmode = a.env->NewStringUTF("w");
    int fd = (int)a.env->CallStaticIntMethod(a.cls, openFdID, juri, jmode);
    a.env->DeleteLocalRef(jmode);
    if (a.env->ExceptionCheck())
    {
        a.env->ExceptionClear();
        LIBRADOR_LOG(LOG_ERROR, "openFileDescriptor threw for daq file %s", filepath);
        return nullptr;
    }
    if (fd < 0)
    {
        LIBRADOR_LOG(LOG_ERROR, "openFileDescriptor failed for daq file %s", filepath);
        return nullptr;
    }

    FILE* fp = fdopen(fd, "w");
    if (fp == nullptr)
    {
        LIBRADOR_LOG(LOG_ERROR, "fdopen failed for daq file %s", filepath);
        return nullptr;
    }

    SDL_IOStreamInterface iface;
    SDL_INIT_INTERFACE(&iface);
    iface.size = fileStreamSize;
    iface.seek = fileStreamSeek;
    iface.read = fileStreamRead;
    iface.write = fileStreamWrite;
    iface.close = fileStreamClose;
    return SDL_OpenIO(&iface, new FileStreamCtx { fp });
}

// Copy the firmware image out of the APK assets to external storage so
// libdfuprog (which reads plain files) can flash it.
const char* android_prepare_firmware_hex(const char* filename)
{
    ActivityRef a;
    jfieldID asset_manager_id
        = a.env->GetFieldID(a.cls, "mgr", "Landroid/content/res/AssetManager;");
    jobject mgr_java = (jobject)a.env->GetObjectField(a.activity, asset_manager_id);
    const char* external_filepath = SDL_GetAndroidExternalStoragePath();
    AAssetManager* mgr = AAssetManager_fromJava(a.env, mgr_java);

    char apk_firmware_filepath[128];
    snprintf(apk_firmware_filepath, sizeof apk_firmware_filepath, "firmware/%s", filename);

    static char firmware_copy_filepath[256];
    snprintf(firmware_copy_filepath, sizeof firmware_copy_filepath, "%s/%s",
        external_filepath, filename);
    LIBRADOR_LOG(LOG_DEBUG, "firmware copy path: %s", firmware_copy_filepath);

    AAsset* asset = AAssetManager_open(mgr, apk_firmware_filepath, AASSET_MODE_STREAMING);
    if(asset == nullptr)
    {
        LIBRADOR_LOG(LOG_ERROR, "firmware asset missing from APK: %s", apk_firmware_filepath);
        return nullptr;
    }
    char buf[2048];
    int nb_read = 0;
    FILE* out = fopen(firmware_copy_filepath, "w+");
    while ((nb_read = AAsset_read(asset, buf, 2048)) > 0)
        fwrite(buf, nb_read, 1, out);
    fclose(out);
    AAsset_close(asset);
    return firmware_copy_filepath;
}

} // namespace

void librador_register_android_hooks()
{
    librador_host_hooks hooks;
    hooks.request_firmware_flash = android_request_firmware_flash;
    hooks.confirm_firmware_flash = android_confirm_firmware_flash;
    hooks.set_bootloader_mode_allowed = android_set_bootloader_mode_allowed;
    hooks.daq_file_written = android_daq_file_written;
    hooks.open_daq_stream = android_open_daq_stream;
    hooks.prepare_firmware_hex = android_prepare_firmware_hex;
    librador_set_host_hooks(hooks);
}

#endif // __ANDROID__
