#ifndef PSUUI_H
#define PSUUI_H

#include "ui_tile.h"
class psuUI : public UI_tile
{
    const std::chrono::milliseconds between_usb_sends_min{100};
    float psu = 4.5f;
    std::chrono::steady_clock::time_point last_usb_send;
    bool need_usb_send = false;
public:
    psuUI() : UI_tile("PSU", "PSU", UI_tile::Width::duplex, 1) {};
    void usb_send_data();
    void draw(float width, inputsUI* inputs_ui = nullptr) override;
    int get_height() override;
};
#endif // PSUUI_H
