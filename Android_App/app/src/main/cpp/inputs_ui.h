#ifndef INPUTSUI_H
#define INPUTSUI_H

#include "ui_tile.h"
class inputsUI : public UI_tile
{
    bool scope750 = false;
    bool changed = false;
public:
    inputsUI() : UI_tile("Inputs","Inputs",UI_tile::Width::singlet, 6) {};
    void update_device_mode();
    bool logic_enable[2] = {0,0};
    bool scope_enable[2] = {true,false};
    void draw(float width, inputsUI* inputs_ui = nullptr) override;
    bool changed_since_last();
    bool ch_enabled(int ch);
    enum Mode {Ch1Scope,ScopeLogic,ScopeScope,Ch1Logic,LogicLogic,None,Scope750,Multimeter};
    Mode mode = Mode::Ch1Scope;
    bool mm = false; //multimeter
    bool logic_on();
    bool scopelogic_mode();
    int get_height() override;
    bool logic_AB_enabled(int ch);
};

#endif // INPUTSUI_H
