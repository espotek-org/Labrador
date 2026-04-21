#ifndef PLOTUI_H
#define PLOTUI_H

#include "inputs_ui.h"
#include <vector>

#define GRAPH_SAMPLES 512
class plotUI
{
    double xmin = -.5;
    double xmax = 0.;
    double ymin = -2.;
    double ymax = 2.;
    const double max_time_window_375khz = 10;
    const double max_voltage = 20;
    double x_constraint_min = -max_time_window_375khz;
    double x_constraint_max = 0.;
    const double min_window_size = 1.e-6;
    const double min_voltage_diff = 1.e-3;

    double x_ref_1;
    double x_ref_2;
    double y_ref_1;
    double y_ref_2;

    bool cursor_drag_tool_toggle; // determines which of the two reference lines for a given axis is begin dragged
    bool enable_x_ref_lines = false;
    bool enable_y_ref_lines = false;

public:
    void recompute_x_bounds(bool mode_changed, inputsUI::Mode mode);
    void draw(bool iso_thread_active, inputsUI::Mode mode, bool chA_enabled, bool chB_enabled, double data_width, double plot_height);
    double delay;
    double time_window;
};
#endif // PLOTUI_H
