#ifndef SIGGENUI_H
#define SIGGENUI_H

#include <chrono>
#include "ui_tile.h"

class sigGenUI : public UI_tile
{
    const static int n_bases = 5;
    const int freq_slider_bases[n_bases] = {1,1,1,1000,1000};
    const float freq_slider_maxima[n_bases] = {10.f,100.f,1000.f,10000.f,64000.f};
    const char* freq_slider_formats[n_bases] = {"%.1f Hz", "%.0f Hz", "%.0f Hz", "%.1f kHz", "%.1f kHz"};
    const char* freq_slider_suffixs[n_bases] = {"Hz", "Hz", "Hz", "kHz", "kHz"};
    const char* wf_names[4] = { "Sin", "Square", "Triangle", "Sawtooth" };
    const std::chrono::milliseconds between_usb_sends_min{100};

    void amp_or_min_slider_and_button(const char* slider_label, const char* button_label, float *amp_or_min, float *amp_or_min_delayed, float *min_or_amp, float *min_or_amp_delayed);
    std::chrono::steady_clock::time_point last_usb_send;
    struct ch_data {
        int wf = 0;
        int slider_base = 2;
        float freq = 500.f;
        float amp = 0.f;
        float min_val = 0.f;
        float amp_delayed = 0.f;
        float min_val_delayed = 0.f;
    };
    ch_data both_ch_data[2];
    ch_data * curr_ch_data = both_ch_data; // helper
    bool need_usb_send;
    int ch_sel = 1;
public:
    sigGenUI() : UI_tile("Signal Generator", "Signal Generator", UI_tile::Width::duplex, 6) {};
    void draw(float width, inputsUI* inputs_ui = nullptr) override;
    void usb_send_data(int ch);
    int get_height() override;
};

#endif // SIGGENUI_H
