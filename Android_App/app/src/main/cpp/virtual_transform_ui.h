#ifndef VIRTUALTRANSFORMUI_H
#define VIRTUALTRANSFORMUI_H

#include "ui_tile.h"
class virtualTransformUI : public UI_tile
{
    static const int num_gain_options = 3;
    const int gains[num_gain_options] = {1, 5, 10};
    const char* gain_labels[num_gain_options] = {"1x", "5x", "10x"};
    struct ch_settings {
        float offset = 0.f;
        int gain_sel = 0;
        bool is_ac = false;
        bool is_paused = false;
    };

    int ch_sel = 1;
    ch_settings both_ch_settings[2];
    ch_settings* curr_ch_settings = both_ch_settings;
public:
    virtualTransformUI() : UI_tile("Virtual Transforms", "Virtual Transforms", UI_tile::Width::duplex, 4) {};
    void draw(float width, inputsUI* inputs_ui = nullptr) override;
    int get_height() override;
};
#endif // VIRTUALTRANSFORMUI_H
