#pragma once

#include "ui/desktop/DesktopFrontend.h"

// Tablet layout: the desktop layout re-dressed for fingers. Targets Android
// tablets in the 1024x768 / 1280x720 class (and is selectable on desktop for
// testing via View > Layout or LABRADOR_LAYOUT=tablet).
//
// The desktop layout is already fully font-driven (panel width, widget sizes,
// toolbar, rail all derive from the current font), so the tablet variant is
// the same arrangement rendered under a touch-sized style: taller frames,
// hit slop around every item, and fat grabs/scrollbars. Everything else —
// widgets, menus, pages, settings keys — is shared with the desktop.
class TabletFrontend : public DesktopFrontend
{
  protected:
    void renderLayout(App& app) override;
};
