#ifndef DAQUI_H
#define DAQUI_H

#include "ui_tile.h"
#include <SDL3/SDL.h>
class daqUI : public UI_tile
{
    bool scope750 = false;
    bool changed = false;
    static const int num_unit_options = 2;
    const char* units_labels[num_unit_options] = {"Volts", "ADC"};
    bool units_sel;
    float duration;
    bool doA, doB;
    float timer;
    bool timer_on;
    int in_sample_rate;
    const ImU8   u8_one  = 1;
    ImU8   downsample_factor  = 1;

public:
    daqUI() : UI_tile("DAQ","DAQ",UI_tile::Width::singlet, 8) {};
    void draw(float width, inputsUI* inputs_ui = nullptr) override;
    bool changed_since_last();
    int get_height() override;
};

#endif // DAQUI_H
