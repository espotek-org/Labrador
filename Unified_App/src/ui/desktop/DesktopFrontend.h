#pragma once

#include "ui/InstrumentFrontend.h"

class App;

// Desktop form factor — the faithful port of the Monash LabraScope arrangement
// (iterated with several dozen students; the tested reference). All the widget
// machinery lives in InstrumentFrontend; this class supplies only the layout.
class DesktopFrontend : public InstrumentFrontend
{
  protected:
    void renderLayout(App& app) override;
};
