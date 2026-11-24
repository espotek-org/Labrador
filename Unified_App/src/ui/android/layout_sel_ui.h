#ifndef LAYOUTSELUI_H
#define LAYOUTSELUI_H

#include "ui_tile.h"
#include "app/App.h"
class layoutSelUI : public UI_tile
{
public:
    layoutSelUI() : UI_tile("Layout", "Layout", UI_tile::Width::singlet, 1) {};
    void draw(float width, inputsUI* inputs_ui) override;
    int get_height() override;
    App::LayoutMode layout_mode = App::LayoutMode::Mobile;

};
#endif // PSUUI_H
