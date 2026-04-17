#ifndef TRIGGERUI_H
#define TRIGGERUI_H

#include "o1buffer.h"
#include "ui_tile.h"
#include "inputs_ui.h"
class triggerUI : public UI_tile
{
    int ch_sel = 1;
    o1buffer::trigger_settings both_ch_trigger_settings[2];
    o1buffer::trigger_settings* curr_ch_trigger_settings = &both_ch_trigger_settings[ch_sel-1];
public:
    triggerUI() : UI_tile("Trigger","Trigger",UI_tile::Width::duplex, 5) {};
    void draw(float width, inputsUI* inputs_ui = nullptr) override;
    int get_height() override;
};
#endif
