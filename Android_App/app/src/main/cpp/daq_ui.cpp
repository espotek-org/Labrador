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
void daqUI::draw(float width_pixels, inputsUI* inputs_ui)
{
    static bool first_time = true;

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::BeginGroup();
    standard_header(width_pixels);
    if(!is_expanded)
    {
        ImGui::EndGroup();
        return;
    }

#define INDENTRIGHT ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(style.CellPadding.x,0.f));
    JNIEnv *env = (JNIEnv *) SDL_GetAndroidJNIEnv();
    jobject MainActivityObject = (jobject) SDL_GetAndroidActivity();
    jclass MainActivity(env->GetObjectClass(MainActivityObject));
    jmethodID mfvID = env->GetMethodID(MainActivity, "getDocsDir", "()Ljava/lang/String;");
    static jstring docsdir;
    if(first_time) {
        docsdir = (jstring)env->CallObjectMethod(MainActivityObject, mfvID);
        first_time = false;
    }
    const char* storage_dir = env->GetStringUTFChars(docsdir,0);

    char user_path[path_size];

    strcpy(user_path, "/Documents/Labrador");
    strcat(user_path, "/");
    strcpy(full_path, storage_dir);
    strcat(full_path, "/");

    ImGuiIO& io = ImGui::GetIO(); (void)io;
    SDL_PropertiesID* propsIme = (SDL_PropertiesID*) io.UserData; 

    ImGui::BeginGroup(); // for bounding rect
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{0.f,0.f});
    ImGui::Dummy(ImVec2(width_pixels,0.f));
    ImGui::PopStyleVar();

    ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f, style.ItemInnerSpacing.y));
    INDENTRIGHT
    ImGui::PushItemWidth(width_pixels - 2 * style.ItemInnerSpacing.x);
    ImGui::InputText("##iddaq", file_name, IM_COUNTOF(file_name));
    SKOIA;

//     https://developer.android.com/reference/android/R.attr#inputType
    if(ImGui::IsItemActivated()) {
        SDL_SetNumberProperty(*propsIme, SDL_PROP_TEXTINPUT_ANDROID_INPUTTYPE_NUMBER, 1);
    } else if (ImGui::IsItemDeactivated()) {
        SDL_SetNumberProperty(*propsIme, SDL_PROP_TEXTINPUT_ANDROID_INPUTTYPE_NUMBER, 2|2002);
    }
    if(strcmp(file_name,"")==0) 
        strcpy(file_name, "filename");

    strcat(user_path, file_name);
    strcat(user_path, ".txt");// must have .txt suffix to allow mediascanner to index the file as a Document, put it in Recents
    strcat(full_path, file_name);
    strcat(full_path, ".txt");// must have .txt suffix to allow mediascanner to index the file as a Document, put it in Recents
    INDENTRIGHT
    ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(-style.ItemInnerSpacing.x + (width_pixels - (ImGui::CalcTextSize("File path").x + style.FramePadding.x*2))/2, 0.));
    ImGui::Button("File path");
    static bool hovered_last_frame = false;
    // block below: prevent inadvertent inputs to other widgets when closing the tooltip
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)||hovered_last_frame) {
        ImGui::SetKeyOwner(ImGuiKey_MouseLeft, ImGui::GetItemID());
        hovered_last_frame = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
    }
    if(ImGui::BeginItemTooltip()){
        ImGui::PushTextWrapPos(800);
        ImGui::Text("%s", user_path);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    INDENTRIGHT
    ImGui::BeginDisabled(timer_on);
    if(timer_on) {
        ImGui::InputFloat("##Timedaq", &timer, 0.f, 0.f, "%.1f s");
    } else {
        ImGui::InputFloat("##Timedaq", &duration, 0.f, 0.f, "%.1f s");
    }
    SKOIA;
    ImGui::EndDisabled();
    duration = ImMin(duration, 10.f);
    duration = ImMax(duration, 0.f);
    duration = IM_ROUND(duration * 10)/10.f;


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
        ImGui::RadioButton("A ", &ch_sel, 1);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ALIGN_Y
        ImGui::BeginDisabled(!inputs_ui->ch_enabled(2));
        ImGui::RadioButton("B", &ch_sel, 2); 
        ImGui::EndDisabled();
    ImGui::PopStyleVar();
    INDENTRIGHT
    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha,1.0);
    int ch_units_sel = units_sel[ch_sel-1] + 1;
    int ch_num_unit_options = num_unit_options[ch_sel - 1];
    if(ImGui::BeginCombo("##daqunits", units_labels[inputs_ui->logic_AB_enabled(ch_sel)][ch_units_sel])) {
        for(int n=0; n < ch_num_unit_options + 1; n++) {
            if(ImGui::Selectable(units_labels[inputs_ui->logic_AB_enabled(ch_sel)][n], n==ch_units_sel, (n==0 ?  ImGuiSelectableFlags_Disabled : ImGuiSelectableFlags_None))) {
                units_sel[ch_sel-1] = n - 1;// n-1 because n=0 is the header
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar();

    INDENTRIGHT

    int in_sample_rate = librador_get_samples_per_second();
    if(inputs_ui->logic_AB_enabled(ch_sel)) {
        ImGui::Text("%.4g kSa/s", static_cast<float>(8 * in_sample_rate/1e3)/downsample_factor);
    } else {
        ImGui::Text("%.4g kSa/s", static_cast<float>(in_sample_rate/1e3)/downsample_factor);
    }
    ImGui::PushItemWidth(width_pixels - ImGui::CalcTextSize("dwn").x - style.ItemInnerSpacing.x);
    INDENTRIGHT
    ImGui::InputScalar("##dwndaq", ImGuiDataType_U8, &downsample_factor,  &u8_one, NULL, "%ux", ImGuiInputTextFlags_None);
    downsample_factor = ImMax(downsample_factor,u8_one);
    SKOIA;
    INDENTRIGHT
    ImGui::BeginDisabled(daq_converting_and_saving);
    if(ImGui::Button("Begin")) {
        timer_on = true;
        timer = 0.f;
    } else if(timer_on) {
        timer += io.DeltaTime;
    }
    ImGui::SameLine();
    bool doA = (strcmp(units_labels[inputs_ui->logic_AB_enabled(1)][units_sel[0] + 1], "None") != 0) && inputs_ui->ch_enabled(1);
    bool doB = (strcmp(units_labels[inputs_ui->logic_AB_enabled(2)][units_sel[1] + 1], "None") != 0) && inputs_ui->ch_enabled(2);
    if(ImGui::Button("End") || (timer >= duration)) {
        timer_on = false;
        timer = -1.f;
        if(doA||doB)
            daq_converting_and_saving = true;
        if(doA && doB)
            librador_daq(3, (duration * in_sample_rate) / downsample_factor, downsample_factor, units_sel, full_path);
        else if(doA)
            librador_daq(1, (duration * in_sample_rate) / downsample_factor, downsample_factor, units_sel, full_path);
        else if(doB)
            librador_daq(2, (duration * in_sample_rate) / downsample_factor, downsample_factor, units_sel, full_path);
    }
    ImGui::EndDisabled();


    ImGui::EndGroup();
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax() + ImVec2(0.f,style.FramePadding.y);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRect(p0, p1, IM_COL32(90, 90, 120, 255));
    ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f,style.FramePadding.y - style.ItemSpacing.y));
    ImGui::Dummy({0.f,0.f}); // prevents issue with this draw() command affecting the vertical alignment of whatever ui element comes after it
    ImGui::EndGroup();
}

void daqUI::poll_status()
{
    if(daq_converting_and_saving)
        daq_converting_and_saving = librador_poll_daq_status();
}

int daqUI::get_height()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x, style.CellPadding.y * 2));// if this line is active, make sure that the line that resets CellPadding at the end of this function is active as well
    style = ImGui::GetStyle();
    int height = 2 * style.ItemSpacing.y + ImGui::GetFontSize() + \
                 ImGui::GetFontSize() + style.ItemInnerSpacing.y + style.ItemSpacing.y + \
                 7 * (ImGui::GetFontSize() + (style.FramePadding.y)*2 + style.ItemSpacing.y);

    ImGui::PopStyleVar();
    return height;
}

