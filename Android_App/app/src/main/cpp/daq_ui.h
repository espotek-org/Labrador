#ifndef DAQUI_H
#define DAQUI_H

#include "ui_tile.h"
#include <SDL3/SDL.h>
class daqUI : public UI_tile
{
    bool scope750 = false;
    bool changed = false;
    static const int num_unit_options = 3;
    const char* analog_unit_labels[num_unit_options] = {"Units:","Volts", "ADC"};
    const char* digital_unit_labels[num_unit_options] = {"Units:","Bits", "Chars"};
    const char** units_labels[2] = {analog_unit_labels, digital_unit_labels};
    int units_sel[2] = {0,0};
    float duration;
    bool doA, doB;
    int ch_sel = 1;
    bool daq_converting_and_saving = false;
    float timer = -1.f;
    bool timer_on = false;
    int in_sample_rate;
    const ImU8   u8_one  = 1;
    ImU8   downsample_factor  = 1;
    static const int path_size = 128;

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
