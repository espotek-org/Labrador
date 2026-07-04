#pragma once

#include "ui/InstrumentFrontend.h"

class App;

// Low-resolution / compact form factor for 800x480-class LCDs (Raspberry Pi
// builds): plot on the left, tabbed control column on the right so everything
// fits 480px height. All the widget machinery lives in InstrumentFrontend;
// this class supplies only the layout.
class LowResFrontend : public InstrumentFrontend
{
  protected:
    void renderLayout(App& app) override;
};
