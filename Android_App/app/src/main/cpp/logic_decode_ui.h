#ifndef LOGICDECODEUI_H
#define LOGICDECODEUI_H

#include "ui_tile.h"
#include "uartstyledecoder.h"

class logicDecodeUI : public UI_tile
{
    enum class Protocol {UART, I2C};
    Protocol protocol_sel = Protocol::UART;

    static const int num_baud_options = 12;
    const int baud_rates[num_baud_options] = {      
      300,
      600,
      1200,
      2400,
      4800,
      9600,
      14400,
      19200,
      28800,
      38400,
      57600,
      115200
    };
    const char* baud_rate_labels[num_baud_options+1] = {
      "Baud:",
      "300",
      "600",
      "1200",
      "2400",
      "4800",
      "9600",
      "14400",
      "19200",
      "28800",
      "38400",
      "57600",
      "115200"};

    static const int num_parity_options = 3;
    const UartParity parities[num_parity_options] = {UartParity::None, UartParity::Even, UartParity::Odd};
    const char* parity_labels[num_parity_options+1] = {"Parity:", "None", "Even", "Odd"};

    struct uart_settings {
        bool decode_on = false;
        int baud_idx_sel = 0;
        int parity_idx_sel = 0;
    };

    uart_settings both_ch_uart_settings[2];
    float ch_console_height[2] = {0.f, 0.f};
    bool draw_uart_settings(float grabber_height, float width_pixels);
    float init_console_height_per_ch = 300.f;
    float grabber_height;
    float grabber1_backlog = 0.f;
    float grabber2_backlog = 0.f;
    float grabber_delta_tracker2 = 0.f;
    float draw_grabber(float grabber_height, const char * label, float* backlog, int ch_idx, bool parity_check);
    void print_stream(int id, const char * text, bool *at_bottom, float window_content_width, float ch_console_height);
    bool uart_ch_console_at_bottom[2] = {true, true};
    bool i2c_console_at_bottom = true;
    float get_grabber_height();
public:
    logicDecodeUI() : UI_tile("Logic Decoding", "Logic Dec.", UI_tile::Width::singlet, 4) {};
    bool decoding_on();
    void draw(float width, inputsUI* inputs_ui = nullptr) override;
    void update(inputsUI* inputs_ui);
    void draw_console(float window_content_width);//const char * from_librador_1, const char * from_librador_2 = nullptr);
    int get_height() override;
};
#endif // LOGICDECODEUI_H
