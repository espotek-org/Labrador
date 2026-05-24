#ifndef DAQUI_H
#define DAQUI_H

#include "ui_tile.h"
class daqUI : public UI_tile
{
    bool scope750 = false;
    bool changed = false;
public:
    daqUI() : UI_tile("DAQ","DAQ",UI_tile::Width::duplex, 6) {};
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
};

#endif // DAQUI_H
