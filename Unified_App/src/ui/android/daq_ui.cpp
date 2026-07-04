#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "librador.h"
#include "custom_imgui.h"
#include <chrono>
#include "imgui_internal.h"
#include "daq_ui.h"
#include <SDL3/SDL.h>
#include "imgui_impl_sdl3.h"
#include "inputs_ui.h"
#ifndef PLATFORM_ANDROID
#include "platform/paths.h" // getPrefPath() — desktop writable DAQ output dir
#include <cstring>
#endif

void daqUI::draw(float width_pixels, inputsUI* inputs_ui)
{
    static bool first_time = true;
    float rhsx = ImGui::GetCursorScreenPos().x + width_pixels;

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::BeginGroup();
    standard_header(width_pixels);
    if(!is_expanded)
    {
        first_time = true; // allow re-request of write permission (Android <= 9) when the tile is collapsed/re-expanded
        ImGui::EndGroup();
        return;
    }
    if(first_time && !dir_initiated) {
        daqUI::init_file_dir();
    }
    first_time = false;
    ImGui::BeginDisabled(!dir_initiated);

#define INDENTRIGHT ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(style.CellPadding.x,0.f));

    char user_path[path_size];

    strcpy(user_path, "/Documents/Labrador");
    strcat(user_path, "/");
    strcpy(full_path, storage_dir);
    strcat(full_path, "/");

    ImGuiIO& io = ImGui::GetIO(); (void)io;
#ifdef PLATFORM_ANDROID
    // Numeric on-screen keyboard hint (Android only; io.UserData is unset on
    // desktop, where physical keyboards need no per-field input-type switch).
    SDL_PropertiesID* propsIme = (SDL_PropertiesID*) io.UserData;
#endif

    ImGui::BeginGroup(); // for bounding rect
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{0.f,0.f});
    ImGui::Dummy(ImVec2(width_pixels,0.f));
    ImGui::PopStyleVar();

    ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f, style.ItemInnerSpacing.y));
    INDENTRIGHT
    ImGui::PushItemWidth(width_pixels - 2 * style.CellPadding.x - 2 *style.FramePadding.x - ImGui::CalcTextSize("?").x);
    ImGui::InputText("##iddaq", file_name, IM_COUNTOF(file_name));
    SKOIA;

//     https://developer.android.com/reference/android/R.attr#inputType
#ifdef PLATFORM_ANDROID
    if(ImGui::IsItemActivated()) {
        SDL_SetNumberProperty(*propsIme, SDL_PROP_TEXTINPUT_ANDROID_INPUTTYPE_NUMBER, 1);
    } else if (ImGui::IsItemDeactivated()) {
        SDL_SetNumberProperty(*propsIme, SDL_PROP_TEXTINPUT_ANDROID_INPUTTYPE_NUMBER, 2|2002);
    }
#endif
    if(daq_finished) {
        daq_finished = false;
        timer2 = 1;
        strcpy(file_name2, file_name);
    }
    if(timer2 > 0) {
        timer2 -= io.DeltaTime;
        if(timer2 > 0) {
            strcpy(file_name, "Saved");
        } else {
            strcpy(file_name, file_name2);
        }
    } else if(strcmp(file_name,"")==0 || strstr(file_name,"/")!=nullptr) {
        strcpy(file_name, "filename");
    }

    strcat(user_path, file_name);
    strcat(user_path, ".txt");// must have .txt suffix to allow mediascanner to index the file as a Document, put it in Recents
    strcat(full_path, file_name);
    strcat(full_path, ".txt");// must have .txt suffix to allow mediascanner to index the file as a Document, put it in Recents
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
    ImGui::SameLine();
    ImGui::PopStyleVar();
    ImGui::Button("?");
    static bool hovered_last_frame = false;
    // block below: prevent inadvertent inputs to other widgets when closing the tooltip
    if(ImGui::IsItemClicked()||hovered_last_frame) {
        ImGui::SetKeyOwner(ImGuiKey_MouseLeft, ImGui::GetItemID());
        hovered_last_frame = !hovered_last_frame ? true : ImGui::IsItemHovered();
    }
    if(hovered_last_frame && ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x);
        ImGui::Text("%s", user_path);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    INDENTRIGHT
    ImGui::BeginDisabled(timer_on);
    ImGui::PushItemWidth(width_pixels - ImGui::CalcTextSize("\xee\xa4\x85").x - style.CellPadding.x - style.ItemSpacing.x);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {style.ItemSpacing.x/2,style.ItemSpacing.y});
    if(timer_on) {
        ImGui::InputFloat("##Timedaq", &timer, 0.f, 0.f, "%.4f s");
    } else {
        ImGui::InputFloat("##Timedaq", &duration, 0.f, 0.f, "%.3f s");
    }
    SKOIA;
    ImGui::SameLine();
    ImGui::Text("\xee\xa4\x85");
    ImGui::PopStyleVar();
    ImGui::EndDisabled();
    duration = ImMin(duration, 10.f);
    duration = ImMax(duration, 0.f);
    duration = IM_ROUND(duration * 1000)/1000.f;


    ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f, (ImGui::GetFontSize() + 2 * style.FramePadding.y - ImGui::CalcTextSize("CH: ").y)/2));
#define ALIGN_Y    ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() - ImVec2(0.f, (ImGui::GetFontSize() + 2 * style.FramePadding.y - ImGui::CalcTextSize("CH: ").y)/2));
    INDENTRIGHT
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f,style.ItemSpacing.y));
//         ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f, style.FramePadding.y - style.ItemSpacing.y));
// #define ALIGN_Y ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() - ImVec2(0.f, style.FramePadding.y - style.ItemSpacing.y));
        ImGui::Text("CH:");
        ImGui::SameLine();
        ALIGN_Y
        ImGui::BeginDisabled(!inputs_ui->ch_enabled(1));
        ImGui::RadioButton("A", &ch_sel, 1);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(style.ItemInnerSpacing.x, 0.f));
        ALIGN_Y
        ImGui::BeginDisabled(!inputs_ui->ch_enabled(2));
        ImGui::RadioButton("B", &ch_sel, 2); 
        ImGui::EndDisabled();
    ImGui::PopStyleVar();
    INDENTRIGHT

    // calls to librador_daq require that the selected units are valid
    for(int ch: {1,2}) {
        units_sel[ch-1] = (inputs_ui->ch_enabled(ch) && (usbCallHandler::daqUnitIsForScope[units_sel[ch-1]] != inputs_ui->logic_AB_enabled(ch))) ? units_sel[ch-1] : usbCallHandler::daqUnitOptions::None;
    }

    int ch_units_sel = units_sel[ch_sel - 1];
    ImGui::BeginDisabled(!inputs_ui->ch_enabled(ch_sel));
    if(ImGui::BeginCombo("##daqunits", usbCallHandler::daq_unit_labels[ch_units_sel])) {
        ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha,1.0);
        ImGui::Selectable("Record:", false, ImGuiSelectableFlags_Disabled);
        ImGui::PopStyleVar();
        for(int n=0; n < usbCallHandler::daqUnitOptions::QUANT; n++) {
            if(n!=usbCallHandler::daqUnitOptions::None && usbCallHandler::daqUnitIsForScope[n] == inputs_ui->logic_AB_enabled(ch_sel))
                continue;
            if(ImGui::Selectable(usbCallHandler::daq_unit_labels[n], n==ch_units_sel)) {
                units_sel[ch_sel-1] = (usbCallHandler::daqUnitOptions) n;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    INDENTRIGHT

    float sample_rate_base = !inputs_ui->ch_enabled(ch_sel) ? 0 : (inputs_ui->mode == inputsUI::Mode::Scope750 ? 750 : 375);
    float sample_rate = sample_rate_base * (inputs_ui->logic_AB_enabled(ch_sel) ? 8 : 1);
    ImGui::Text("%.4g kSa/s", sample_rate/downsample_factor);
    ImGui::PushItemWidth(width_pixels - ImGui::CalcTextSize("\xee\xa4\x86").x - style.FramePadding.x - style.ItemSpacing.x);
    INDENTRIGHT
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {style.ItemSpacing.x/2,style.ItemSpacing.y});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, {0.,style.ItemSpacing.y});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::InputScalar("##dwndaq", ImGuiDataType_U8, &downsample_factor,  &u8_one, NULL, "%u", ImGuiInputTextFlags_None);
    SKOIA;
    ImGui::SameLine();
    ImGui::Text("\xee\xa4\x86");
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    downsample_factor = ImMax(downsample_factor,u8_one);
    bool doA = (units_sel[0] != usbCallHandler::daqUnitOptions::None) && inputs_ui->ch_enabled(1);
    bool doB = (units_sel[1] != usbCallHandler::daqUnitOptions::None) && inputs_ui->ch_enabled(2);
    INDENTRIGHT
    ImGui::BeginDisabled(daq_converting_and_saving || !(doA || doB) || duration == 0.f);
    if(ImGui::Button("Begin")) {
        timer_on = true;
        timer = 0.f;
    } else if(timer_on) {
        timer += io.DeltaTime;
    }
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(rhsx - 2*style.FramePadding.x - ImGui::CalcTextSize("End").x - style.CellPadding.x,ImGui::GetCursorScreenPos().y));

    if(ImGui::Button("End") || (timer >= duration)) {
        timer_on = false;
        timer = -1.f;
        if(doA||doB)
            daq_converting_and_saving = true;
        if(doA && doB)
            librador_daq(3, 1e3*(duration * sample_rate_base) / downsample_factor, downsample_factor, units_sel, full_path);
        else if(doA)
            librador_daq(1, 1e3*(duration * sample_rate_base) / downsample_factor, downsample_factor, units_sel, full_path);
        else if(doB)
            librador_daq(2, 1e3*(duration * sample_rate_base) / downsample_factor, downsample_factor, units_sel, full_path);
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled(); // !dir_initiated


    ImGui::EndGroup();
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax() + ImVec2(0.f,style.FramePadding.y);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRect(p0, p1, IM_COL32(90, 90, 120, 255));
    ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f,style.FramePadding.y - style.ItemSpacing.y));
    ImGui::Dummy({0.f,0.f}); // prevents issue with this draw() command affecting the vertical alignment of whatever ui element comes after it
    ImGui::EndGroup();
}

void daqUI::init_file_dir()
{
#ifdef PLATFORM_ANDROID
    static jstring docsdir;
    JNIEnv *env = (JNIEnv *) SDL_GetAndroidJNIEnv();
    jobject MainActivityObject = (jobject) SDL_GetAndroidActivity();
    jclass MainActivity(env->GetObjectClass(MainActivityObject));
    jmethodID mfvID = env->GetMethodID(MainActivity, "getDocsDir", "(J)Ljava/lang/String;");
    docsdir = (jstring)env->CallObjectMethod(MainActivityObject, mfvID, (jlong) &dir_initiated);
    strcpy(storage_dir, env->GetStringUTFChars(docsdir,0));
#else
    // Desktop: DAQ files land in the per-user writable app dir (Brent used the
    // Android SAF Documents dir via JNI; there is no storage-permission step to
    // wait for on desktop, so mark the directory ready immediately). draw()
    // appends "/" + filename + ".txt", so strip getPrefPath()'s trailing slash.
    std::string dir = getPrefPath();
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\'))
        dir.pop_back();
    std::strncpy(storage_dir, dir.c_str(), path_size - 1);
    storage_dir[path_size - 1] = '\0';
    dir_initiated = true;
#endif
}

void daqUI::poll_status()
{
    if(daq_converting_and_saving) {
        daq_converting_and_saving = librador_poll_daq_status();
        daq_finished = true;
    }
}

int daqUI::get_height()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x, style.CellPadding.y * 2));// if this line is active, make sure that the line that resets CellPadding at the end of this function is active as well
    style = ImGui::GetStyle();
    int height = 2 * style.ItemSpacing.y + ImGui::GetFontSize() + \
                 ImGui::GetFontSize() + style.ItemInnerSpacing.y + style.ItemSpacing.y + \
                 6 * (ImGui::GetFontSize() + (style.FramePadding.y)*2 + style.ItemSpacing.y);

    ImGui::PopStyleVar();
    return height;
}

#ifdef PLATFORM_ANDROID
JNIEXPORT void JNICALL Java_com_EspoTek_Labrador_MainActivity_nativeExternalStoragePermissionUpdate(JNIEnv *env, jobject thisobject, jlong dir_initiated_ptr_from_java)
{
    // modify c++ member variable using a call from java
    bool* dir_initiated_ptr = (bool *) dir_initiated_ptr_from_java;
    *dir_initiated_ptr = true;
}

#endif

