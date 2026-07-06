#include "ui/tablet/TabletFrontend.h"
#include "instruments/UIComponents.hpp" // ScaledPx

void TabletFrontend::renderLayout(App& app)
{
    // Touch-sized style for the whole frame; the desktop layout computes its
    // chrome (toolbar/status heights, panel width, rail) from the font and
    // frame metrics, so it reflows around these without any layout changes.
    // Save/restore the full style rather than push/pop: TouchExtraPadding
    // has no ImGuiStyleVar, and a plain copy keeps the two in lockstep.
    ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiStyle style_backup = style;
    style.FramePadding = ImVec2(10, 9);      // frame height ~= font + 18 px
    style.ItemSpacing.y = ScaledPx(8.0f);    // room between stacked controls
    style.TouchExtraPadding = ImVec2(6, 6);  // hit slop beyond the visuals
    style.GrabMinSize = ScaledPx(24.0f);     // draggable slider/scroll grabs
    style.ScrollbarSize = ScaledPx(22.0f);   // finger-draggable scrollbars

    DesktopFrontend::renderLayout(app);

    style = style_backup;
}
