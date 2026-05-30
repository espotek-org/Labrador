#ifndef DAQUI_H
#define DAQUI_H

#include "ui_tile.h"
#include "usbcallhandler.h"
#include <SDL3/SDL.h>

#ifdef PLATFORM_ANDROID
#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif
JNIEXPORT void JNICALL Java_com_EspoTek_Labrador_MainActivity_nativeExternalStoragePermissionUpdate(JNIEnv *, jobject, jlong);
#ifdef __cplusplus
}
#endif
#endif // PLATFORM_ANDROID
class daqUI : public UI_tile
{
    bool scope750 = false;
    bool changed = false;
    usbCallHandler::daqUnitOptions units_sel[2] = {usbCallHandler::daqUnitOptions::None, usbCallHandler::daqUnitOptions::None};

    float duration;
    int ch_sel = 1;
    bool daq_converting_and_saving = false;
    float timer = -1.f;
    bool timer_on = false;
    int in_sample_rate;
    const ImU8   u8_one  = 1;
    ImU8   downsample_factor  = 1;
    static const int path_size = 128;
    void init_file_dir();
    char storage_dir[path_size];
    bool dir_initiated = false;
    friend void Java_com_EspoTek_Labrador_MainActivity_nativeExternalStoragePermissionUpdate(JNIEnv *, jobject, jlong);
public:
    daqUI() : UI_tile("DAQ","DAQ",UI_tile::Width::singlet, 8) {};
    void draw(float width, inputsUI* inputs_ui = nullptr) override;
    void poll_status();
    bool changed_since_last();
    int get_height() override;
    char full_path[path_size]; 
    char file_name[path_size/2]; 
};

#endif // DAQUI_H
