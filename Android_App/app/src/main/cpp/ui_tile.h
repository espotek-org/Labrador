#ifndef UITILE_H
#define UITILE_H
class inputsUI;

class UI_tile
{
    public:
        virtual ~UI_tile() {};
        enum Width {singlet, duplex};
        UI_tile(const char* name, const char* short_name, Width width, int n_lines) : name(name), short_name(short_name), width(width), n_lines(n_lines) {};
        virtual int get_height() = 0;
        int get_collapsed_height();
        virtual void draw(float width_pixels, inputsUI* inputs_ui = nullptr) = 0;
        const Width width;
        int n_lines;
        const char* name;
        const char* short_name;
        void standard_header(float width_pixels);
        bool is_expanded = true;
        bool next_is_expanded = true; // to prevent expansion/contraction of tiles mid-frame
        bool is_visible = true;
};
#endif
