#pragma once
#include "imgui.h"
#include "implot.h"
#include "librador.h"
#include "algorithm"
#include <chrono>
#include "OSCControl.hpp"
#include "OscData.hpp"
#include "fftw3.h"
#include <fstream>
#include <iostream>
#include <float.h>
#include <math.h>
#include "ControlWidget.hpp"
#include "NetworkAnalyser.hpp"
#include "AnalysisToolsWidget.hpp"
#include "implot_internal.h"
#include <chrono>
#include "util.h"
using Clock = std::chrono::steady_clock;



/// <summary>
/// Renders oscilloscope data
/// </summary>
class PlotWidget : public ControlWidget
{
public:
	int currentLabOscGain = 4;
	double time_of_last_gain_update = 0;
	bool SpectrumAcquisitionExists = false;

	// Visible y-range of the main plot (displayed volts), captured each
	// frame the time or eye view renders — drives the view-based hardware
	// auto-gain (Qt behaviour). Both of those views put displayed volts on
	// the y axis; XY does not qualify (its y axis is OSC2 volts only, x is
	// OSC1), so it and the spectrum/network views leave this invalid.
	double VisibleYMin = 0.0;
	double VisibleYMax = 0.0;
	bool VisibleYValid = false;

	// Where the plot-help "?" lives: under the plot (classic/lowres) or
	// hosted by the desktop toolbar (which sets show_help itself).
	bool ShowHelpButton = true;

	/// <summary>
	/// Constructor
	/// </summary>
	/// <param name="label">Name of controller</param>
	/// <param name="size">Child window size</param>
	PlotWidget(const char* label, ImVec2 size, const float* borderColour)
	    : ControlWidget(label, size, borderColour)
	    , size(size)
	    , osc_control(osc_control)
	{
		librador_set_oscilloscope_gain(1<<currentLabOscGain);
		ImPlot::GetInputMap().Fit = -1; // Disable double-click to fit so that it doesn't conflict with double-click to reset
	}

	/// <summary>
	/// Update size of child window
	/// </summary>
	/// <param name="new_size">New size</param>
	void setSize(ImVec2 new_size)
	{
		size = new_size;
	}
	void SetNetworkAnalyser(NetworkAnalyser* na, NetworkAnalyser::Config* na_cfg)
	{
		if (na != this->na)
		{
			this->na = na;
			this->na_cfg = na_cfg;
		}
	}
	void SetControllers(OSCControl* osc_control, AnalysisToolsWidget* analysis_tools_widget)
	{
		if (osc_control != this->osc_control)
		{
			this->osc_control = osc_control;
		}
		if(analysis_tools_widget != this->analysis_tools_widget)
		{
			this->analysis_tools_widget = analysis_tools_widget;
		}
	}


	/// <summary>
	/// Generic function to render plot widget with correct style
	/// </summary>
	void Render() override
	{
		static Clock::time_point last = Clock::now();
		Clock::time_point now = Clock::now();
		double dt_s = std::chrono::duration<double>(now - last).count(); // seconds since last call
		last = now;
		VisibleYValid = false; // set true only when the time view renders

		// Optional: show it
		// printf("Frame dt: %.3f ms  (%.1f FPS)\n", dt_s * 1000.0, 1.0 / std::max(dt_s, 1e-9));
		// Show cursor deltas if they are both active
		bool show_cursor_props = (osc_control->Cursor1toggle && osc_control->Cursor2toggle);
		// Show text underneath if cursors or signal properties activated
		bool text_window = show_cursor_props || osc_control->SignalPropertiesToggle;

		// --- Compute available region ---
		ImVec2 region_size = ImGui::GetContentRegionAvail();
		const ImGuiStyle& st = ImGui::GetStyle();
		float row_height = ImGui::GetFrameHeightWithSpacing();

		// Bottom UI: help button + small cursor props table row
		// (your current layout has the help button and the tiny 1-row table).
		float bottom_misc_h = row_height; // one line worth

		// If cursor props are shown, add one more line (the values row you add)
		if (osc_control->Cursor1toggle && osc_control->Cursor2toggle) {
			bottom_misc_h += row_height;
		}

		// If Signal Properties toggle is on, reserve its height up front.
		// We assume phase table is shown if you want it included in the layout.
		float signal_props_h = 0.0f;
		if (osc_control->SignalPropertiesToggle) {
			// how many signals you pass below (OSC1, OSC2, Math)
			const int signal_count = 3;
			const bool include_phase = true; // set false if you don't want to reserve for phase table
			signal_props_h = EstimateSignalPropsHeight(signal_count, include_phase);
		}

		// Total reserved height for bottom UI
		float reserved_bottom_h = bottom_misc_h + signal_props_h;

		// Safety clamp so we never go negative
		float usable_height = std::max(0.0f, region_size.y - reserved_bottom_h);

		// --- Count how many vertical plots we need ---
		int plot_count = 1; // base oscilloscope plot (always)

		const bool show_spectrum = (analysis_tools_widget->ToolsOn && analysis_tools_widget->CurrentTab == 0);
		const bool show_network = (analysis_tools_widget->ToolsOn && analysis_tools_widget->CurrentTab == 1 && na != nullptr);

		if (show_spectrum) plot_count++;
		if (show_network)  plot_count++;

		// Safety: avoid div-by-zero; still give minimal height if nothing else is drawn
		if (plot_count <= 0) plot_count = 1;

		// --- Divide vertical space evenly ---
		float per_plot_height = usable_height / (float)plot_count - 3; // don't know why it overflows plot area, reduce per_plot_height by 3 to fix it

		// Optional: enforce a minimum plot height
		const float min_plot_h = 120.0f;
		if (per_plot_height < min_plot_h) {
			per_plot_height = min_plot_h;
		}

		// --- Sizes for each plot section ---
		ImVec2 plot_size = ImVec2(region_size.x, per_plot_height);

		// Spectrum Analyser plots
		ImVec2 plot_size_spectrum = ImVec2(0, 0);
		ImVec2 plot_size_spectrum_magnitude = ImVec2(0, 0);
		if (show_spectrum) {
			plot_size_spectrum = ImVec2(region_size.x, per_plot_height);
			plot_size_spectrum_magnitude = plot_size_spectrum;
		}

		// Network Analyser plots
		ImVec2 plot_size_network = ImVec2(0, 0);
		ImVec2 plot_size_network_magnitude = ImVec2(0, 0);
		ImVec2 plot_size_network_phase = ImVec2(0, 0);

		if (show_network) {
			plot_size_network = ImVec2(region_size.x, per_plot_height);
			plot_size_network_magnitude = plot_size_network;

			if (analysis_tools_widget->NA.PhaseOn) {
				// Split horizontally: magnitude on the left, phase on the right
				plot_size_network_magnitude.x *= 0.5f;
				plot_size_network_phase = plot_size_network_magnitude;
			}
		}



		
		ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 255));
		UpdateOscData();
		std::vector<double> analog_data_osc1 = OSC1Data->GetData();
		std::vector<double> analog_data_osc2 = OSC2Data->GetData();

		// --- XY mode (Qt setXYmode parity) ---
		// When on, the main plot becomes OSC1 voltage (x) vs OSC2 voltage (y).
		// The XY plot uses its own ImPlot ID so the time plot's zoom state
		// survives mode round-trips.
		const bool xy_mode = osc_control->XYMode;
		// --- Eye diagram (Qt on_actionEye_Diagram_triggered parity) ---
		// When on, the main plot becomes overlaid trigger-aligned sweeps of
		// OSC1. OSCControl keeps the two view toggles mutually exclusive;
		// XY wins here if both are somehow set.
		const bool eye_mode = !xy_mode && osc_control->EyeDiagram;
		// 0 = time series, 1 = XY, 2 = eye diagram
		const int main_view = xy_mode ? 1 : (eye_mode ? 2 : 0);
		// Fits a voltage axis around a data vector (default range if empty)
		auto xy_fit = [&](const std::vector<double>& v, double& lo, double& hi)
		{
			lo = init_voltage_range_lower;
			hi = init_voltage_range_upper;
			if (!v.empty())
			{
				double v_min = *std::min_element(v.begin(), v.end());
				double v_max = *std::max_element(v.begin(), v.end());
				double v_range = v_max - v_min;
				// signal fills ~80% of the axis; flat line gets 1 V of air
				double pad = v_range == 0 ? 1.0 : 0.5 * v_range * (1 / 0.8 - 1);
				lo = v_min - pad;
				hi = v_max + pad;
			}
		};
		if (main_view != prev_main_view)
		{
			// Axes change meaning between views (time vs volts vs
			// sweep-relative time): drop pending reset/fit requests aimed at
			// another view's plot.
			next_resetX = false;
			next_resetY = false;
			next_autofitX = false;
			next_autofitY = false;
			if (xy_mode)
			{
				// Entering XY: fit both axes to the live data, like Qt's
				// freshly reset xmin/xmax accumulators in setXYmode
				xy_fit(analog_data_osc1, next_autofitX_min, next_autofitX_max);
				xy_fit(analog_data_osc2, next_autofitY_min, next_autofitY_max);
				next_autofitX = true;
				next_autofitY = true;
			}
			// Entering the eye view: fit the axes once sweeps exist (the
			// first eye is only computed inside the plot below)
			eye_fit_pending = eye_mode;
			prev_main_view = main_view;
		}

		if (xy_mode)
		{
			ImPlot::SetNextAxesLimits(init_voltage_range_lower, init_voltage_range_upper,
			    init_voltage_range_lower, init_voltage_range_upper, ImPlotCond_Once);
			if (next_resetX)
			{
				ImPlot::SetNextAxisLimits(ImAxis_X1, init_voltage_range_lower, init_voltage_range_upper, ImPlotCond_Always);
				next_resetX = false;
			}
			if (next_resetY)
			{
				ImPlot::SetNextAxisLimits(ImAxis_Y1, init_voltage_range_lower, init_voltage_range_upper, ImPlotCond_Always);
				next_resetY = false;
			}
		}
		else if (eye_mode)
		{
			// Qt's eye view opens on ±1 ms / ±12 V (isodriver.h display3)
			ImPlot::SetNextAxesLimits(init_eye_time_range_lower, init_eye_time_range_upper,
			    init_eye_voltage_range_lower, init_eye_voltage_range_upper, ImPlotCond_Once);
			if (next_resetX)
			{
				ImPlot::SetNextAxisLimits(ImAxis_X1, init_eye_time_range_lower, init_eye_time_range_upper, ImPlotCond_Always);
				next_resetX = false;
			}
			if (next_resetY)
			{
				ImPlot::SetNextAxisLimits(ImAxis_Y1, init_eye_voltage_range_lower, init_eye_voltage_range_upper, ImPlotCond_Always);
				next_resetY = false;
			}
		}
		else
		{
			ImPlot::SetNextAxesLimits(init_time_range_lower, init_time_range_upper,
			    init_voltage_range_lower, init_voltage_range_upper, ImPlotCond_Once);
			if (next_resetX)
			{
				ImPlot::SetNextAxisLimits(ImAxis_X1, init_time_range_lower, init_time_range_upper, ImPlotCond_Always);
				next_resetX = false;
			}
			if (next_resetY)
			{
				ImPlot::SetNextAxisLimits(ImAxis_Y1, init_voltage_range_lower, init_voltage_range_upper, ImPlotCond_Always);
				next_resetY = false;
			}
		}
		if (next_autofitX)
		{
			ImPlot::SetNextAxisLimits(ImAxis_X1, next_autofitX_min, next_autofitX_max, ImPlotCond_Always);
			next_autofitX = false;
		}
		if (next_autofitY)
		{
			ImPlot::SetNextAxisLimits(ImAxis_Y1, next_autofitY_min, next_autofitY_max, ImPlotCond_Always);
			next_autofitY = false;
		}

		// --- before BeginPlot ---
		ImPlotFlags base_plot_flags = ImPlotFlags_NoFrame | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus;

		ImPlotFlags plot_flags = base_plot_flags;

		if (trig_dragging) {
			// Option A (strict): block all plot inputs (pan, zoom, box select, etc.)
			plot_flags |= ImPlotFlags_NoInputs;

		}
		if (xy_mode)
		{
			// Voltage-vs-voltage: constrain both axes to equal units/pixel
			if (ImPlot::BeginPlot("##OscilloscopesXY", plot_size, plot_flags | ImPlotFlags_Equal))
			{
				ImPlot::SetupAxes("", "", ImPlotAxisFlags_NoLabel, ImPlotAxisFlags_NoLabel);
				ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, -20, 20);
				ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, -20, 20);
				ImPlot::SetupAxisFormat(ImAxis_X1, MetricFormatter, (void*)"V");
				ImPlot::SetupAxisFormat(ImAxis_Y1, MetricFormatter, (void*)"V");

				// Double-click to autofit, Ctrl+double-click to reset (same
				// gestures as the time-domain plot)
				if (ImPlot::IsPlotHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl))
					{
						next_resetX = true;
						next_resetY = true;
					}
					else
					{
						osc_control->AutofitX = true;
						osc_control->AutofitY = true;
					}
				}
				// Auto Fit buttons: x follows OSC1 volts, y follows OSC2 volts
				if (osc_control->AutofitX)
				{
					xy_fit(analog_data_osc1, next_autofitX_min, next_autofitX_max);
					next_autofitX = true;
				}
				if (osc_control->AutofitY)
				{
					xy_fit(analog_data_osc2, next_autofitY_min, next_autofitY_max);
					next_autofitY = true;
				}

				// One curve: OSC1 voltage on x against OSC2 voltage on y.
				// Like Qt, plot whatever data exists (only meaningful when
				// both channels are scope inputs).
				const int xy_count = (int)std::min(analog_data_osc1.size(), analog_data_osc2.size());
				if (xy_count > 0)
				{
					ImPlot::PlotLine("##XY", analog_data_osc1.data(), analog_data_osc2.data(), xy_count,
						ImPlotSpec(ImPlotProp_LineWeight, 2.5f, ImPlotProp_LineColor, osc_control->OSC1Colour.Value));
				}

				// Semi-transparent mode label (matches the Spectrum inlay)
				{
					const ImPlotRect lim = ImPlot::GetPlotLimits();
					ImPlot::PushStyleColor(ImPlotCol_InlayText, ImVec4(1, 1, 1, 0.40f));
					ImPlot::Annotation(lim.X.Min, lim.Y.Max, ImVec4(0, 0, 0, 0), ImVec2(8, -6),
						true, "XY: OSC1 (x) vs OSC2 (y)");
					ImPlot::PopStyleColor();
				}

				// Cursors, trigger markers and the math trace are time-domain
				// concepts; Qt hides them in XY mode and so do we.
				ImPlot::EndPlot();
			}
		}
		else if (eye_mode)
		{
			// Overlaid trigger-aligned sweeps of OSC1 (Qt isodriver.cpp
			// plot() eye branch). Own ImPlot ID so the time plot's zoom
			// state survives view round-trips, like the XY plot.
			if (ImPlot::BeginPlot("##OscilloscopesEye", plot_size, plot_flags))
			{
				ImPlot::SetupAxes("", "", ImPlotAxisFlags_NoLabel, ImPlotAxisFlags_NoLabel);
				ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, -20, 20);
				ImPlot::SetupAxisFormat(ImAxis_X1, MetricFormatter, (void*)"s");
				ImPlot::SetupAxisFormat(ImAxis_Y1, MetricFormatter, (void*)"V");

				// Double-click to autofit, Ctrl+double-click to reset (same
				// gestures as the time-domain plot)
				if (ImPlot::IsPlotHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl))
					{
						next_resetX = true;
						next_resetY = true;
					}
					else
					{
						osc_control->AutofitX = true;
						osc_control->AutofitY = true;
					}
				}

				// Sweeps are aligned by the existing trigger machinery
				// (OscData::GetEyeDiagram reuses the GetTriggerTime crossing
				// search), so the eye only exists while the trigger is on.
				// The sweeps always show OSC1 (Qt's eye is CH1-only); the
				// edge type follows the trigger combo.
				int eye_count = 0;
				if (osc_control->Trigger)
				{
					constants::TriggerType eye_trigger_type
					    = maps::ComboItemToChannelTriggerPair.at(osc_control->TriggerTypeComboCurrentItem).trigger_type;
					eye_count = OSC1Data->GetEyeDiagram(eye_trigger_type,
					    osc_control->TriggerLevel.getValue(),
					    osc_control->TriggerHysteresis, osc_control->EyeTraces);
				}
				const std::vector<double>& eye_time = OSC1Data->EyeTime();
				const std::vector<std::vector<double>>& eye_traces = OSC1Data->EyeTraces();

				// Auto Fit: x spans the sweep window exactly; y rounds out to
				// the next volt (both straight from Qt's eye branch, which
				// ranges x to ±samplesPerSymbol and y to round(min/max)∓1)
				if ((osc_control->AutofitX || osc_control->AutofitY || eye_fit_pending)
				    && eye_count > 0 && eye_time.size() >= 2)
				{
					double v_min = DBL_MAX;
					double v_max = -DBL_MAX;
					for (const std::vector<double>& trace : eye_traces)
					{
						v_min = std::min(v_min, *std::min_element(trace.begin(), trace.end()));
						v_max = std::max(v_max, *std::max_element(trace.begin(), trace.end()));
					}
					if (osc_control->AutofitX || eye_fit_pending)
					{
						next_autofitX_min = eye_time.front();
						next_autofitX_max = eye_time.back();
						next_autofitX = true;
					}
					if (osc_control->AutofitY || eye_fit_pending)
					{
						next_autofitY_min = std::round(v_min) - 1;
						next_autofitY_max = std::round(v_max) + 1;
						next_autofitY = true;
					}
					eye_fit_pending = false;
				}

				// Overlay the sweeps in OSC1's colour, faded per trace so
				// stroke density reads as brightness
				if (eye_count > 0)
				{
					ImVec4 eye_colour = osc_control->OSC1Colour.Value;
					eye_colour.w = std::max(0.15f, 1.0f / (float)eye_count);
					char trace_id[16];
					for (int i = 0; i < eye_count; i++)
					{
						const std::vector<double>& trace = eye_traces[i];
						const int count = (int)std::min(eye_time.size(), trace.size());
						snprintf(trace_id, sizeof(trace_id), "##Eye%03d", i);
						ImPlot::PlotLine(trace_id, eye_time.data(), trace.data(), count,
						    ImPlotSpec(ImPlotProp_LineWeight, 2.5f, ImPlotProp_LineColor, eye_colour));
					}
				}

				if (!osc_control->Trigger)
				{
					// Centered empty-state hint, in the same semi-transparent
					// inlay style as the Spectrum/XY labels
					const ImPlotRect lim = ImPlot::GetPlotLimits();
					ImPlot::PushStyleColor(ImPlotCol_InlayText, ImVec4(1, 1, 1, 0.40f));
					ImPlot::Annotation(0.5 * (lim.X.Min + lim.X.Max), 0.5 * (lim.Y.Min + lim.Y.Max),
					    ImVec4(0, 0, 0, 0), ImVec2(0, 0), true, "Enable trigger for eye diagram");
					ImPlot::PopStyleColor();
				}

				// Semi-transparent mode label (matches the Spectrum/XY inlays)
				{
					const ImPlotRect lim = ImPlot::GetPlotLimits();
					ImPlot::PushStyleColor(ImPlotCol_InlayText, ImVec4(1, 1, 1, 0.40f));
					ImPlot::Annotation(lim.X.Min, lim.Y.Max, ImVec4(0, 0, 0, 0), ImVec2(8, -6),
					    true, "Eye: OSC1, %d traces", eye_count);
					ImPlot::PopStyleColor();
				}

				// The y axis shows OSC1's displayed volts, so the eye view
				// publishes the visible range for view-driven auto-gain just
				// like the time view (XY can't: its y axis is OSC2 volts).
				VisibleYMin = ImPlot::GetPlotLimits().Y.Min;
				VisibleYMax = ImPlot::GetPlotLimits().Y.Max;
				VisibleYValid = true;

				// Cursors, trigger markers and the math trace are time-series
				// concepts; hidden here like in the XY view.
				ImPlot::EndPlot();
			}
		}
		else if (ImPlot::BeginPlot("##Oscilloscopes", plot_size, plot_flags))
		{
			// No Labels for plot
			ImPlot::SetupAxes("", "", ImPlotAxisFlags_NoLabel, ImPlotAxisFlags_NoLabel);
			// Ensure plot cannot be expanded larger than the size of the buffer
			ImPlot::SetupAxisZoomConstraints(ImAxis_X1, 0, 60);
			ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, -20, 20);
			// --- Double-click to reset plot limits ---
			// Setup Axis to show units
			ImPlot::SetupAxisFormat(ImAxis_X1, MetricFormatter, (void*)"s");
			ImPlot::SetupAxisFormat(ImAxis_Y1, MetricFormatter, (void*)"V");
			if (ImPlot::IsPlotHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
				{
					next_resetX = true;
					next_resetY = true;
				}
				else
				{
					osc_control->AutofitX = true;
					osc_control->AutofitY = true;
				}

			}
			if (ImPlot::IsAxisHovered(ImAxis_X1) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl))
				{
					next_resetX = true;
				}
				else
				{
					osc_control->AutofitX = true;
				}
			}
			if (ImPlot::IsAxisHovered(ImAxis_Y1) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl))
				{
					next_resetY = true;
				}
				else
				{
					osc_control->AutofitY = true;
				}
			}
			// Autofit X so that 3 periods occur in a time window
			// If there are 2 oscs visible use the one with the larger period
			// If none are visible, reset to default
			if (osc_control->AutofitX)
			{
				double T_osc1 = OSC1Data->GetTimeBetweenTriggers();
				double T_osc2 = OSC2Data->GetTimeBetweenTriggers();
				double T = init_time_range_upper;
				if (osc_control->DisplayCheckOSC1 && !osc_control->DisplayCheckOSC2)
				{
					T = T_osc1;
					T *= 3;
				}
				else if (osc_control->DisplayCheckOSC2 && !osc_control->DisplayCheckOSC1)
				{
					T = T_osc2;
					T *= 3;
				}
				else if (osc_control->DisplayCheckOSC2 && osc_control->DisplayCheckOSC1)
				{
					T = T_osc1 > T_osc2 ? T_osc1 : T_osc2;
					T *= 3;
				}
				else if (!osc_control->DisplayCheckOSC2 && !osc_control->DisplayCheckOSC1)
				{
					T = init_time_range_upper;
				}
				// if no data, 0 period, set to default range
				T = T == 0 ? init_time_range_upper : T;
				next_autofitX_min = -T / 2;
				next_autofitX_max = T / 2;
				next_autofitX = true;
			}
			// Autofits Y
			if (osc_control->AutofitY)
			{
				double osc1_max;
				double osc2_max;
				double osc1_min;
				double osc2_min;
				if (osc_control->DisplayCheckOSC1) // set to actual max and min only if the oscilloscope channel is visible
				{
					osc1_max = OSC1Data->GetDataMax();
					osc1_min = OSC1Data->GetDataMin();
				}
				else // else set to opposite extremes so that it always loses on comparison
				{
					osc1_max = DBL_MIN;
					osc1_min = DBL_MAX;
				}
				if (osc_control->DisplayCheckOSC2) // set to actual max and min only if the oscilloscope channel is visible
				{
					osc2_max = OSC2Data->GetDataMax();
					osc2_min = OSC2Data->GetDataMin();
				}
				else // else set to opposite extremes so that it always loses on comparison
				{
					osc2_max = DBL_MIN;
					osc2_min = DBL_MAX;
				}
				double osc_max = osc1_max > osc2_max ? osc1_max : osc2_max;
				double osc_min = osc1_min < osc2_min ? osc1_min : osc2_min;
				double osc_range = osc_max - osc_min;
				double osc_frac = 0.3;
				double pad = 0.5 * osc_range * (1 / osc_frac - 1);
				// set to default
				next_autofitY_min = init_voltage_range_lower;
				next_autofitY_max = init_voltage_range_upper;
				if (osc_control->DisplayCheckOSC1 || osc_control->DisplayCheckOSC2) // only autofit if there is visible data
				{
					next_autofitY_min = osc_min - pad;
					next_autofitY_max = osc_max + pad;
				}
				next_autofitY = true;
			}
			// Plot oscilloscope 1 signal
			std::vector<double> time_osc1 = OSC1Data->GetTime();
			if (osc_control->DisplayCheckOSC1)
			{
				// bugfixed: only set colour if line is being draw.
				ImPlot::PlotLine("##Osc 1", time_osc1.data(), analog_data_osc1.data(),
					analog_data_osc1.size(),
					ImPlotSpec(ImPlotProp_LineWeight, 2.5f, ImPlotProp_LineColor, osc_control->OSC1Colour.Value));
			}
			// Set OscData Time Vector to match the current X-axis
			OSC1Data->SetTime(ImPlot::GetPlotLimits().X.Min, ImPlot::GetPlotLimits().X.Max);
			// Publish the visible voltage range for view-driven auto-gain
			VisibleYMin = ImPlot::GetPlotLimits().Y.Min;
			VisibleYMax = ImPlot::GetPlotLimits().Y.Max;
			VisibleYValid = true;
			// Plot Oscilloscope 2 Signal
			std::vector<double> time_osc2 = OSC2Data->GetTime();
			if (osc_control->DisplayCheckOSC2)
			{
				ImPlot::PlotLine("##Osc 2", time_osc2.data(), analog_data_osc2.data(),
					analog_data_osc2.size(),
					ImPlotSpec(ImPlotProp_LineWeight, 2.5f, ImPlotProp_LineColor, osc_control->OSC2Colour.Value));
			}
			// Set OscData Time Vector to match the current X-axis
			OSC2Data->SetTime(ImPlot::GetPlotLimits().X.Min, ImPlot::GetPlotLimits().X.Max);
			// Plot Math Signal
			// --- Math Signal ---
			// choose a time base
			std::vector<double> time_math = (time_osc1.size() >= time_osc2.size()) ? time_osc1 : time_osc2;
			if (time_math.empty()) {
				// create time vector based on current x-axis limits if both oscs are empty
				// use number of points based on a 375000 Hz sample rate
				double x_min = ImPlot::GetPlotLimits().X.Min;
				double x_max = ImPlot::GetPlotLimits().X.Max;
				const double sample_rate = 375000.0;
				size_t num_points = static_cast<size_t>((x_max - x_min) * sample_rate);
				time_math.resize(num_points);
				for (size_t i = 0; i < num_points; i++) {
					time_math[i] = x_min + (static_cast<double>(i) / static_cast<double>(num_points)) * (x_max - x_min);
				}
			}
			std::string expr = osc_control->MathControls1.Text;
			ParseStatus parse_status;

			std::vector<double> math_data = EvalUserExpression(expr, analog_data_osc1, analog_data_osc2, time_math, parse_status);

			if (parse_status.success) {
				osc_control->MathControls1.Parsable = true;

				if (osc_control->MathControls1.On) {
					

					// if result is empty or time base is empty, clear and bail
					if (math_data.empty() || time_math.empty()) {
						MathData->SetData({});                 // <<< clear stale MATH buffer
					}
					else {
						// update MathData only when we actually have samples to show
						MathData->SetTime(ImPlot::GetPlotLimits().X.Min, ImPlot::GetPlotLimits().X.Max, time_math);
						MathData->SetData(math_data);
						ImPlot::PlotLine("##Math", time_math.data(), math_data.data(), (int)math_data.size(),
							ImPlotSpec(ImPlotProp_LineWeight, 2.5f, ImPlotProp_LineColor, osc_control->MathColour.Value));
					}
				}
				else {
					// toggle is OFF → clear any previous math samples
					MathData->SetData({});                     // <<< clear when OFF
				}
			}
			else {
				// parse failed → not parsable, clear any previous math samples
				osc_control->MathControls1.Parsable = false;
				MathData->SetData({});                         // <<< clear on parse failure
			}

			// Plot cursors
			if (osc_control->Cursor1toggle)
				drawCursor(1, &cursor1_x, &cursor1_y);
			if (osc_control->Cursor2toggle)
				drawCursor(2, &cursor2_x, &cursor2_y);

			DrawAndDragTriggers();
			

			ImPlot::EndPlot();
		}


		// --- Spectrum Analyser ---
		// Always-run acquisition (independent of visibility/tab)
		if (analysis_tools_widget->SA.Acquire) {
			const int   sr = analysis_tools_widget->SA.SampleRatesValues[analysis_tools_widget->SA.SampleRatesComboCurrentItem];
			const float tw = analysis_tools_widget->SA.TimeWindow;
			const int   widx = analysis_tools_widget->SA.WindowComboCurrentItem;

			OSC1Data->PerformSpectrumAnalysis(sr, tw, widx);
			OSC2Data->PerformSpectrumAnalysis(sr, tw, widx);

			analysis_tools_widget->SA.OSC1_last_captured = tw;
			analysis_tools_widget->SA.OSC2_last_captured = tw;
		}

		// ---- Choose magnitude pointers by units (for both channels) ----
		// Units: 0=dBm, 1=dBV, 2=V RMS
		std::vector<double>* mag1 = nullptr;
		std::vector<double>* mag2 = nullptr;
		AxisLimitRanges magnitude_range = magnitude_db_range;

		switch (analysis_tools_widget->SA.UnitsComboCurrentItem) {
		default:
		case 0: // dBm
			mag1 = OSC1Data->GetSpectrumMagdBm_p();
			mag2 = OSC2Data->GetSpectrumMagdBm_p();
			magnitude_range = magnitude_db_range;
			break;
		case 1: // dBV
			mag1 = OSC1Data->GetSpectrumMagdBV_p();
			mag2 = OSC2Data->GetSpectrumMagdBV_p();
			magnitude_range = magnitude_db_range;
			break;
		case 2: // V RMS
			mag1 = OSC1Data->GetSpectrumMag_p();
			mag2 = OSC2Data->GetSpectrumMag_p();
			magnitude_range = magnitude_VRMS_range;
			break;
		}

		std::vector<double>* f1 = OSC1Data->GetSpectrumFreq_p();
		std::vector<double>* f2 = OSC2Data->GetSpectrumFreq_p();

		const bool has1 = (mag1 && !mag1->empty() && f1 && f1->size() >= 2);
		const bool has2 = (mag2 && !mag2->empty() && f2 && f2->size() >= 2);

		// Update existence flag even if we don't draw this frame
		analysis_tools_widget->SA.AcquisitionExists = has1 || has2;

		// -------- Plot only if Tools are visible AND Spectrum tab is active --------
		if (analysis_tools_widget->ToolsOn && analysis_tools_widget->CurrentTab == 0) {
			ImPlotFlags spectrum_base_plot_flags = ImPlotFlags_NoFrame | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus;

			// Combined limits
			double combined_xmin = init_freq_range_lower, combined_xmax = init_freq_range_upper;
			double combined_ymin = magnitude_range.init_lower, combined_ymax = magnitude_range.init_upper;

			if (has1 || has2) {
				double xlo = std::numeric_limits<double>::infinity();
				double xhi = 0.0;
				if (has1) { xlo = std::min(xlo, (*f1)[1]); xhi = std::max(xhi, f1->back()); }
				if (has2) { xlo = std::min(xlo, (*f2)[1]); xhi = std::max(xhi, f2->back()); }
				if (std::isfinite(xlo) && xhi > 0) { combined_xmin = xlo; combined_xmax = xhi; }

				double gmin = std::numeric_limits<double>::infinity();
				double gmax = -std::numeric_limits<double>::infinity();
				if (has1) {
					gmin = std::min(gmin, *std::min_element(mag1->begin(), mag1->end()));
					gmax = std::max(gmax, *std::max_element(mag1->begin(), mag1->end()));
				}
				if (has2) {
					gmin = std::min(gmin, *std::min_element(mag2->begin(), mag2->end()));
					gmax = std::max(gmax, *std::max_element(mag2->begin(), mag2->end()));
				}
				if (std::isfinite(gmin) && std::isfinite(gmax)) {
					const double range = std::max(1e-12, gmax - gmin);
					const double pad = std::max(1.0, 0.5 * range * (1.0 / 0.9 - 1.0));
					combined_ymin = gmin - pad;
					combined_ymax = gmax + pad;
				}
			}

			// Dynamic Y constraints
			static double constraint_mag_lower = magnitude_db_range.constraint_lower;
			static double constraint_mag_upper = magnitude_db_range.constraint_upper;
			if (!has1 && !has2) {
				constraint_mag_lower = magnitude_range.constraint_lower;
				constraint_mag_upper = magnitude_range.constraint_upper;
			}
			else {
				double cmin = std::numeric_limits<double>::infinity();
				double cmax = -std::numeric_limits<double>::infinity();
				if (has1) { cmin = std::min(cmin, *std::min_element(mag1->begin(), mag1->end())); cmax = std::max(cmax, *std::max_element(mag1->begin(), mag1->end())); }
				if (has2) { cmin = std::min(cmin, *std::min_element(mag2->begin(), mag2->end())); cmax = std::max(cmax, *std::max_element(mag2->begin(), mag2->end())); }
				if (std::isfinite(cmin) && std::isfinite(cmax)) { constraint_mag_lower = cmin - 10.0; constraint_mag_upper = cmax + 10.0; }
				else { constraint_mag_lower = magnitude_range.constraint_lower; constraint_mag_upper = magnitude_range.constraint_upper; }
			}

			// Only do spectrum plotting if shared spectrum_plots has been wired up
			if (spectrum_plots) {
				PlotTrace& spectrum_plot_data_osc1 = spectrum_plots->osc1;
				PlotTrace& spectrum_plot_data_osc2 = spectrum_plots->osc2;

				// State remembered for re-decimation / refit
				static double last_x_min = init_freq_range_lower;
				static double last_x_max = init_freq_range_upper;
				static int    prev_units = analysis_tools_widget->SA.UnitsComboCurrentItem;
				static bool   prev_disp1 = analysis_tools_widget->SA.DisplayOSC1;
				static bool   prev_disp2 = analysis_tools_widget->SA.DisplayOSC2;
				static size_t prev_n1 = 0, prev_n2 = 0;
				static double prev_f1_last = 0.0, prev_f2_last = 0.0;
				static bool   first_plot = true;

				// Data-change detector
				const bool data_changed =
					prev_n1 != (has1 ? f1->size() : 0) ||
					prev_n2 != (has2 ? f2->size() : 0) ||
					prev_f1_last != (has1 ? f1->back() : 0.0) ||
					prev_f2_last != (has2 ? f2->back() : 0.0);

				if (ImPlot::BeginPlot("##SpectrumMagnitude", plot_size_spectrum_magnitude, spectrum_base_plot_flags)) {
					ImPlot::SetupAxisFormat(ImAxis_X1, MetricFormatter, (void*)"Hz");

					// --- Y-axis formatting & label ---
					const int unit = analysis_tools_widget->SA.UnitsComboCurrentItem;
					const char* y_label =
						(unit == 0) ? "Magnitude (dBm)" :
						(unit == 1) ? "Magnitude (dBV)" :
						"Magnitude (V)";

					switch (unit) {
					case 0: // dBm
						ImPlot::SetupAxisFormat(ImAxis_Y1, MetricFormatter, (void*)"dBm");
						break;
					case 1: // dBV
						ImPlot::SetupAxisFormat(ImAxis_Y1, MetricFormatter, (void*)"dBV");
						break;
					case 2: // V RMS
						ImPlot::SetupAxisFormat(ImAxis_Y1, MetricFormatter, (void*)"V");
						break;
					}

					ImPlot::SetupAxesLimits(init_freq_range_lower, init_freq_range_upper,
						magnitude_range.init_lower, magnitude_range.init_upper,
						ImPlotCond_Once);

					if (first_plot ||
						analysis_tools_widget->SA.Autofit ||
						analysis_tools_widget->SA.Acquire ||
						data_changed ||
						prev_units != analysis_tools_widget->SA.UnitsComboCurrentItem) {
						ImPlot::SetupAxesLimits(combined_xmin, combined_xmax,
							combined_ymin, combined_ymax,
							ImGuiCond_Always);
						first_plot = false;
					}

					ImPlot::SetupAxes("Frequency (Hz)", y_label,
						ImPlotAxisFlags_NoLabel, ImPlotAxisFlags_NoLabel);
					ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);

					if (!has1 && !has2) {
						ImPlot::SetupAxisLimitsConstraints(ImAxis_X1,
							init_freq_range_lower,
							init_freq_range_upper);
						ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1,
							magnitude_range.constraint_lower,
							magnitude_range.constraint_upper);
					}
					else {
						ImPlot::SetupAxisLimitsConstraints(ImAxis_X1,
							combined_xmin, combined_xmax);
						ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1,
							constraint_mag_lower,
							constraint_mag_upper);
					}

					// Re-decimate when needed (user zoom/pan or data change)
					const bool must_redecimate =
						analysis_tools_widget->SA.Autofit ||
						analysis_tools_widget->SA.Acquire ||
						(prev_units != analysis_tools_widget->SA.UnitsComboCurrentItem) ||
						(prev_disp1 != analysis_tools_widget->SA.DisplayOSC1) ||
						(prev_disp2 != analysis_tools_widget->SA.DisplayOSC2) ||
						(last_x_min != ImPlot::GetPlotLimits().X.Min) ||
						(last_x_max != ImPlot::GetPlotLimits().X.Max) ||
						data_changed;

					if (must_redecimate) {
						if (has1)
							spectrum_plot_data_osc1 = DecimateLogPlotTrace(*f1, *mag1);
						else
							spectrum_plot_data_osc1 = PlotTrace{};

						if (has2)
							spectrum_plot_data_osc2 = DecimateLogPlotTrace(*f2, *mag2);
						else
							spectrum_plot_data_osc2 = PlotTrace{};

						last_x_min = ImPlot::GetPlotLimits().X.Min;
						last_x_max = ImPlot::GetPlotLimits().X.Max;
						prev_units = analysis_tools_widget->SA.UnitsComboCurrentItem;
						prev_disp1 = analysis_tools_widget->SA.DisplayOSC1;
						prev_disp2 = analysis_tools_widget->SA.DisplayOSC2;
						prev_n1 = has1 ? f1->size() : 0;
						prev_n2 = has2 ? f2->size() : 0;
						prev_f1_last = has1 ? f1->back() : 0.0;
						prev_f2_last = has2 ? f2->back() : 0.0;
					}

					// Plot enabled channels
					if (analysis_tools_widget->SA.DisplayOSC1 && !spectrum_plot_data_osc1.x.empty()) {
						ImPlot::PlotLine("##SpectrumOSC1",
							spectrum_plot_data_osc1.x.data(),
							spectrum_plot_data_osc1.y.data(),
							(int)spectrum_plot_data_osc1.y.size(),
							ImPlotSpec(ImPlotProp_LineWeight, 2.5f, ImPlotProp_LineColor, analysis_tools_widget->OSC1Colour.Value));
					}
					if (analysis_tools_widget->SA.DisplayOSC2 && !spectrum_plot_data_osc2.x.empty()) {
						ImPlot::PlotLine("##SpectrumOSC2",
							spectrum_plot_data_osc2.x.data(),
							spectrum_plot_data_osc2.y.data(),
							(int)spectrum_plot_data_osc2.y.size(),
							ImPlotSpec(ImPlotProp_LineWeight, 2.5f, ImPlotProp_LineColor, analysis_tools_widget->OSC2Colour.Value));
					}

					// Draw semi-transparent "Spectrum" label in top-left
					{
						const ImPlotRect lim = ImPlot::GetPlotLimits();
						ImPlot::PushStyleColor(ImPlotCol_InlayText, ImVec4(1, 1, 1, 0.40f));   // semi-transparent white
						ImPlot::Annotation(
							lim.X.Min, lim.Y.Max,                      // top-left in plot coords
							ImVec4(0, 0, 0, 0),                        // col.w==0 -> use InlayText, no bg
							ImVec2(8, -6),                             // pixel offset (right, up)
							/*clamp=*/true,
							"Spectrum"
						);
						ImPlot::PopStyleColor();
					}

					ImPlot::EndPlot();
				}
			} 
		}




		
		// Network Analyser
		ImPlotFlags network_base_plot_flags = ImPlotFlags_NoFrame | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus;
		std::vector<double> na_freq;
		std::vector<double> na_mag;
		std::vector<double> na_phase;
		AxisLimitRanges network_magnitude_range = magnitude_db_range;
		static double network_constraint_mag_lower = magnitude_db_range.constraint_lower;
		static double network_constraint_mag_upper = magnitude_db_range.constraint_upper;

		// (fixed) track NA units, not SA units
		static int Previous_NAC_UnitsComboCurrentItem = analysis_tools_widget->NA.UnitsComboCurrentItem;

		if (na != nullptr) {
			na_freq = na->freqs();      // Hz, must be > 0 for log scale
			switch (analysis_tools_widget->NA.UnitsComboCurrentItem)
			{
			case 0:
				na_mag = na->mag_dB();
				network_magnitude_range = magnitude_db_range;
				break;
			case 1:
				na_mag = na->mag_linear();
				network_magnitude_range = magnitude_VRMS_range;
				break;
			}
			na_phase = na->phase_rad();  // radians
		}
		else {
			na_freq.clear();
			na_mag.clear();
			na_phase.clear();
		}

		struct XTickBuf { std::vector<double> ticks; };
		static XTickBuf g_na_xticks;

		if (analysis_tools_widget->ToolsOn && analysis_tools_widget->CurrentTab == 1)
		{
			// --- Magnitude Plot ------------------------------------------------------
			if (ImPlot::BeginPlot("##NetworkAnalyser", plot_size_network_magnitude, network_base_plot_flags))
			{
				ImPlot::SetupAxisFormat(ImAxis_X1, MetricFormatter, (void*)"Hz");
				switch (analysis_tools_widget->NA.UnitsComboCurrentItem)
				{
				case 0: ImPlot::SetupAxisFormat(ImAxis_Y1, MetricFormatter, (void*)"dB"); break;
				case 1: ImPlot::SetupAxisFormat(ImAxis_Y1, MetricFormatter, (void*)"V");   break;
				}

				if (analysis_tools_widget->NA.Autofit || na->running() || network_was_off ||
					(Previous_NAC_UnitsComboCurrentItem != analysis_tools_widget->NA.UnitsComboCurrentItem))
				{
					if (!na_freq.empty() && !na_mag.empty()) {
						double mag_max = *std::max_element(na_mag.begin(), na_mag.end());
						double mag_min = *std::min_element(na_mag.begin(), na_mag.end());
						double mag_range = mag_max - mag_min;
						double mag_frac = 0.9;
						double pad = 0.5 * mag_range * (1 / mag_frac - 1);
						pad = pad == 0 ? 1 : pad; // if flat line, add padding of 1
						ImPlot::SetupAxesLimits(na_freq[0], na_freq.back(),
							mag_min - pad, mag_max + pad, ImPlotCond_Always);
						network_constraint_mag_lower = *std::min_element(na_mag.begin(), na_mag.end()) - 10;
						network_constraint_mag_upper = *std::max_element(na_mag.begin(), na_mag.end()) + 10;
					}
					else
					{
						ImPlot::SetupAxesLimits(na_cfg->f_start, na_cfg->f_stop,
							network_magnitude_range.init_lower, network_magnitude_range.init_upper, ImPlotCond_Always);
						network_constraint_mag_lower = network_magnitude_range.constraint_lower;
						network_constraint_mag_upper = network_magnitude_range.constraint_upper;
					}
				}

				if (na->running()) {
					ImPlot::SetupAxisLimits(ImAxis_X1, na_cfg->f_start, na_cfg->f_stop, ImPlotCond_Always);
				}

				// Axes: labels + units
				ImPlot::SetupAxis(ImAxis_X1, "Frequency (Hz)", ImPlotAxisFlags_NoLabel);
				ImPlot::SetupAxis(ImAxis_Y1, "Magnitude (dB)", ImPlotAxisFlags_NoLabel);

				// Log scale on X (use Log10 for VNA-style decades)
				ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
				ImPlot::SetupAxesLimits(na_cfg->f_start, na_cfg->f_stop,
					network_magnitude_range.init_lower, network_magnitude_range.init_upper,
					ImPlotCond_Once);

				if (!na_freq.empty() && !na_mag.empty() && !na->running()) // data exists
					ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, na_freq[0], na_freq.back());
				else // no data
					ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, na_cfg->f_start, na_cfg->f_stop);

				if (na_mag.empty() || na_freq.empty()) // constraints if no data
					ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, network_magnitude_range.constraint_lower, network_magnitude_range.constraint_upper);
				else // constraints if data
					ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, network_constraint_mag_lower, network_constraint_mag_upper);

				// Adaptive ticks from live view (no locking)
				ApplyAdaptiveLogXTicks_Internal(ImAxis_X1);

				// (changed) Use a color from analysis_tools_widget (or replace with your dedicated NetworkAnalyserColour)
				if (!na_freq.empty()) {
					// Guard: log scale requires strictly positive x
					bool x_ok = std::all_of(na_freq.begin(), na_freq.end(), [](double f) { return f > 0; });
					if (x_ok) {
						ImPlot::PlotLine("S-Param |Mag| (dB)", na_freq.data(), na_mag.data(), (int)na_freq.size(),
							ImPlotSpec(ImPlotProp_LineWeight, 2.5f, ImPlotProp_LineColor, GetPlotColour(Plot_Blue)));
					}
				}
				{
					const ImPlotRect lim = ImPlot::GetPlotLimits();
					ImPlot::PushStyleColor(ImPlotCol_InlayText, ImVec4(1, 1, 1, 0.40f));
					ImPlot::Annotation(
						lim.X.Min, lim.Y.Max,
						ImVec4(0, 0, 0, 0),
						ImVec2(8, -6),
						true,
						"Network: Magnitude"
					);
					ImPlot::PopStyleColor();
				}

				ImPlot::EndPlot();
			}

			// --- Phase Plot (optional) -----------------------------------------------
			if (analysis_tools_widget->NA.PhaseOn)
			{
				ImGui::SameLine();
				if (ImPlot::BeginPlot("##NetworkAnalyserPhase", plot_size_network_phase, network_base_plot_flags))
				{
					ImPlot::SetupAxisFormat(ImAxis_X1, MetricFormatter, (void*)"Hz");
					ImPlot::SetupAxisFormat(ImAxis_Y1, MetricFormatter, (void*)"rad");

					if (analysis_tools_widget->NA.Autofit || na->running() || network_was_off ||
						(Previous_NAC_UnitsComboCurrentItem != analysis_tools_widget->NA.UnitsComboCurrentItem))
					{
						if (!na_freq.empty() && !na_phase.empty()) {
							double phase_max = *std::max_element(na_phase.begin(), na_phase.end());
							double phase_min = *std::min_element(na_phase.begin(), na_phase.end());
							double phase_range = phase_max - phase_min;
							double phase_frac = 0.9;
							double pad = 0.5 * phase_range * (1 / phase_frac - 1);
							pad = pad == 0 ? 1 : pad;
							ImPlot::SetupAxesLimits(na_freq[0], na_freq.back(),
								phase_min - pad, phase_max + pad, ImPlotCond_Always);
						}
						else
						{
							ImPlot::SetupAxesLimits(na_cfg->f_start, na_cfg->f_stop,
								-M_PI, M_PI, ImPlotCond_Always);
						}
					}

					if (na->running()) {
						ImPlot::SetupAxisLimits(ImAxis_X1, na_cfg->f_start, na_cfg->f_stop, ImPlotCond_Always);
					}

					ImPlot::SetupAxis(ImAxis_X1, "Frequency (Hz)", ImPlotAxisFlags_AutoFit || ImPlotAxisFlags_NoLabel);
					ImPlot::SetupAxis(ImAxis_Y1, "Phase (rad)", ImPlotAxisFlags_AutoFit || ImPlotAxisFlags_NoLabel);

					ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
					ImPlot::SetupAxesLimits(na_cfg->f_start, na_cfg->f_stop,
						-M_PI, M_PI,
						ImPlotCond_Once);

					if (!na_freq.empty() && !na_mag.empty() && !na->running())
						ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, na_freq[0], na_freq.back());
					else
						ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, na_cfg->f_start, na_cfg->f_stop);

					ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, -M_PI, M_PI);

					ApplyAdaptiveLogXTicks_Internal(ImAxis_X1);

					// (changed) Use a color from analysis_tools_widget
					if (!na_freq.empty()) {
						bool x_ok = std::all_of(na_freq.begin(), na_freq.end(), [](double f) { return f > 0; });
						if (x_ok) {
							ImPlot::PlotLine("angle(S) (rad)", na_freq.data(), na_phase.data(), (int)na_freq.size(),
								ImPlotSpec(ImPlotProp_LineWeight, 2.5f, ImPlotProp_LineColor, GetPlotColour(Plot_Red)));
						}
					}
					{
						const ImPlotRect lim = ImPlot::GetPlotLimits();
						ImPlot::PushStyleColor(ImPlotCol_InlayText, ImVec4(1, 1, 1, 0.40f));
						ImPlot::Annotation(
							lim.X.Min, lim.Y.Max,
							ImVec4(0, 0, 0, 0),
							ImVec2(8, -6),
							true,
							"Network: Phase"
						);
						ImPlot::PopStyleColor();
					}

					ImPlot::EndPlot();
				}
			}

			network_was_off = false;
			Previous_NAC_UnitsComboCurrentItem = analysis_tools_widget->NA.UnitsComboCurrentItem;
		}
		else
		{
			network_was_off = true;
		}
		network_plots->freq = na_freq;
		network_plots->mag = na_mag;
		network_plots->phase = na_phase;


		
		// Help Button (desktop hosts it in the toolbar instead)
		if (ShowHelpButton)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
			if (ImGui::Button(" ? "))
			{
				show_help = true;
			}
			ImGui::PopStyleColor();
			ImGui::SameLine();
		}

		// Signal and Cursor Properties
		if (ImGui::BeginTable("signal_props", 4))
		{
			ImGui::TableSetupColumn("One", ImGuiTableColumnFlags_WidthFixed, 180);
			if (show_cursor_props)
			{
				ImGui::TableNextColumn();
				ImGui::Text("Cursor properties:");
				ImGui::TableNextColumn();
				ImGui::Text(u8"\u2206T = %.2f ms", (cursor2_x - cursor1_x)*1000);
				ImGui::TableNextColumn();
				ImGui::Text(u8"1/\u2206T = %.2f Hz", 1 / (cursor2_x - cursor1_x));
				ImGui::TableNextColumn();
				ImGui::Text(u8"\u2206V = %.2f V", cursor2_y - cursor1_y);
				ImGui::TableNextRow();
			}

			ImGui::EndTable();
		}
		if (osc_control->SignalPropertiesToggle)
		{
			DrawSignalPropertiesPanel(
				std::vector<OscData>{ *OSC1Data, *OSC2Data, *MathData },
				std::vector<ImVec4>{
				osc_control->OSC1Colour.Value,
					osc_control->OSC2Colour.Value,
					osc_control->MathColour.Value
			}
			);
		}
		ImGui::PopStyleColor();

		OSC1Data->GetPeriod();

	}
	// ===== Helpers =====
	static inline bool valid_num(double x) { return std::isfinite(x); }
	static inline double wrap_deg(double a) {
		if (!std::isfinite(a)) return a;
		a = std::fmod(a + 180.0, 360.0);
		if (a < 0) a += 360.0;
		return a - 180.0;
	}
	// Right-align within current column
	static void ValueCellOrDash(double v, const char* fmt) {
		char buf[64];
		if (valid_num(v)) std::snprintf(buf, sizeof(buf), fmt, v);
		else              std::snprintf(buf, sizeof(buf), "—");
		float txt_w = ImGui::CalcTextSize(buf).x;
		float col_w = ImGui::GetColumnWidth();
		float x0 = ImGui::GetCursorPosX();
		float pad = ImGui::GetStyle().CellPadding.x;
		float x = x0 + col_w - txt_w - pad;
		if (x > x0) ImGui::SetCursorPosX(x);
		ImGui::TextUnformatted(buf);
	}

	// ===== Panel =====
	void DrawSignalPropertiesPanel(const std::vector<OscData>& signals_in,
		const std::vector<ImVec4>& colors_in)
	{
		ImGui::PushID(&signals_in);

		// -------------------- 0) Build filtered rows (OSC1/OSC2 by toggles, MATH only if valid) --------------------
		struct Row { const OscData* s; ImVec4 color; std::string name; };
		std::vector<Row> rows; rows.reserve(3);

		auto has_nonempty_signal = [](const OscData& s)->bool {
			// Heuristic “has data” check; swap for s.HasSamples() if you have it.
			return std::isfinite(s.GetVpp()) || std::isfinite(s.GetVrms()) ||
				std::isfinite(s.GetVmax()) || std::isfinite(s.GetVmin());
		};

		// Expecting signals_in = { OSC1, OSC2, MATH } in this order
		// Guard for size but handle gracefully
		const OscData* s1 = (signals_in.size() > 0) ? &signals_in[0] : nullptr;
		const OscData* s2 = (signals_in.size() > 1) ? &signals_in[1] : nullptr;
		const OscData* sm = (signals_in.size() > 2) ? &signals_in[2] : nullptr;

		ImVec4 c1 = (colors_in.size() > 0) ? colors_in[0] : ImGui::GetStyleColorVec4(ImGuiCol_Text);
		ImVec4 c2 = (colors_in.size() > 1) ? colors_in[1] : ImGui::GetStyleColorVec4(ImGuiCol_Text);
		ImVec4 cm = (colors_in.size() > 2) ? colors_in[2] : ImGui::GetStyleColorVec4(ImGuiCol_Text);

		// Include OSC1/OSC2 only if their display toggles are on
		if (s1 && osc_control->DisplayCheckOSC1) rows.push_back(Row{ s1, c1, "OSC1" });
		if (s2 && osc_control->DisplayCheckOSC2) rows.push_back(Row{ s2, c2, "OSC2" });

		// Include MATH only if it has a valid/non-empty signal
		if (sm && has_nonempty_signal(*sm))      rows.push_back(Row{ sm, cm, "MATH" });

		const int ROWS = (int)rows.size();
		const ImGuiStyle& st = ImGui::GetStyle();

		auto total_table_width_fixed = [&](const std::vector<float>& w, int col_count)->float {
			float sum = 0.0f; for (int c = 0; c < col_count; ++c) sum += w[c];
			sum += (col_count + 1) * st.CellPadding.x * 2.0f + 4.0f; // padding/border allowance
			return sum;
		};

		// ============================================================================================
		// 1) SIGNAL PROPERTIES TABLE  (click-to-collapse + auto-collapse, non-stretch, non-resizable)
		// ============================================================================================
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::CollapsingHeader("Signal Properties##sigprops", ImGuiTreeNodeFlags_DefaultOpen)) {

				enum ColID { C_CH = 0, C_T, C_F, C_VPP, C_VMAX, C_VMIN, C_VAVG, C_VRMS, C__COUNT };
				const char* L[C__COUNT] = { "Ch","Period (ms)","Freq (Hz)","Vpp (V)","Vmax (V)","Vmin (V)","Vavg (V)","Vrms (V)" };
				const float W_FULL[C__COUNT] = { 44, 88, 88, 78, 78, 78, 78, 78 };
				const float W_MIN[C__COUNT] = { 44, 12, 12, 12, 12, 12, 12, 12 }; // tight collapsed widths

				static bool userCollapsed[C__COUNT] = { false,false,false,false,false,false,false,false };
				userCollapsed[C_CH] = false; // Channel never collapses
				bool autoCollapsed[C__COUNT] = { false,false,false,false,false,false,false,false };

				std::vector<float> colW(C__COUNT);
				for (int c = 0; c < C__COUNT; ++c)
					colW[c] = (userCollapsed[c] || autoCollapsed[c]) ? W_MIN[c] : W_FULL[c];

				float desired_w = total_table_width_fixed(colW, C__COUNT);
				float avail_w = ImGui::GetContentRegionAvail().x;

				// auto-collapse from rightmost to left until it fits (skip channel col)
				if (desired_w > avail_w) {
					for (int c = C__COUNT - 1; c >= 1 && desired_w > avail_w; --c) {
						if (!userCollapsed[c]) {
							autoCollapsed[c] = true;
							colW[c] = W_MIN[c];
							desired_w = total_table_width_fixed(colW, C__COUNT);
						}
					}
				}

				const ImGuiTableFlags tf =
					ImGuiTableFlags_SizingFixedFit |
					ImGuiTableFlags_RowBg |
					ImGuiTableFlags_BordersOuter |
					ImGuiTableFlags_BordersV |
					ImGuiTableFlags_NoHostExtendX; // don't stretch

				if (ImGui::BeginTable("##sigprops_table", C__COUNT, tf, ImVec2(desired_w, 0.0f))) {
					const ImGuiTableColumnFlags CF = ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize;

					for (int c = 0; c < C__COUNT; ++c)
						ImGui::TableSetupColumn(L[c], CF, colW[c]);

					// header row (click toggles USER collapse; widths update next frame)
					ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
					for (int c = 0; c < C__COUNT; ++c) {
						ImGui::TableSetColumnIndex(c);
						const bool effCollapsed = userCollapsed[c] || autoCollapsed[c];
						ImGui::PushStyleColor(ImGuiCol_Text, effCollapsed ? ImVec4(1, 1, 1, 0.35f) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
						ImGui::Selectable(L[c], false, ImGuiSelectableFlags_AllowDoubleClick);
						bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
						ImGui::PopStyleColor();
						if (clicked && c != C_CH) userCollapsed[c] = !userCollapsed[c];
						if (ImGui::IsItemHovered()) ImGui::SetTooltip(effCollapsed ? "Click to expand" : "Click to collapse");
					}

					// rows
					if (ROWS == 0) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(C_CH);
						ImGui::TextDisabled("No signals");
					}
					else {
						for (int r = 0; r < ROWS; ++r) {
							const OscData& s = *rows[r].s;
							ImGui::TableNextRow();

							ImGui::TableSetColumnIndex(C_CH);
							ImGui::TextColored(rows[r].color, "%s", rows[r].name.c_str());

							double T = s.GetPeriod();
							double f = (std::isfinite(T) && T > 0.0) ? 1.0 / T : std::numeric_limits<double>::quiet_NaN();
							ImGui::TableSetColumnIndex(C_T);   ValueCellOrDash(std::isfinite(T) ? (1000.0 * T) : T, "%.1f");
							ImGui::TableSetColumnIndex(C_F);   ValueCellOrDash(f, "%.1f");

							ImGui::TableSetColumnIndex(C_VPP);  ValueCellOrDash(s.GetVpp(), "%.2f");
							ImGui::TableSetColumnIndex(C_VMAX); ValueCellOrDash(s.GetVmax(), "%.2f");
							ImGui::TableSetColumnIndex(C_VMIN); ValueCellOrDash(s.GetVmin(), "%.2f");
							ImGui::TableSetColumnIndex(C_VAVG); ValueCellOrDash(s.GetVavg(), "%.2f");
							ImGui::TableSetColumnIndex(C_VRMS); ValueCellOrDash(s.GetVrms(), "%.2f");
						}
					}

					ImGui::EndTable();
				}

				ImGui::Dummy(ImVec2(0, 2)); // bottom border breathing room
			}
		}

		// ============================================================================================
		// 2) PHASE DIFFERENCE TABLE  (same collapse rules, uses filtered rows)
		// ============================================================================================
		if (ROWS >= 2) {
			// FIX: correct NaN initialization syntax
			std::vector<double> phi(ROWS, std::numeric_limits<double>::quiet_NaN());
			for (int i = 0; i < ROWS; ++i)
				phi[i] = rows[i].s->GetPhaseDeg();

			bool any_pair = false;
			for (int i = 0; i < ROWS && !any_pair; ++i)
				for (int j = 0; j < ROWS && !any_pair; ++j)
					if (i != j && std::isfinite(phi[i]) && std::isfinite(phi[j]))
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::CollapsingHeader("Phase Difference (degrees)##phase", ImGuiTreeNodeFlags_DefaultOpen)) {
				const int COLS = ROWS + 1;

				std::vector<float> W_FULL(COLS, 84.0f), W_MIN(COLS, 12.0f);
				W_FULL[0] = 96.0f;  W_MIN[0] = 72.0f; // left label col

				static std::unordered_map<std::string, bool> userCollapsedPhase;
				auto key_of = [](const std::string& s) { return std::string("PHASE:") + s; };

				std::vector<std::string> label(COLS);
				label[0] = u8"\u0394\u03A6 (deg)";
				for (int j = 0; j < ROWS; ++j)
					label[j + 1] = rows[j].name;

				std::vector<bool> autoCollapsed(COLS, false);
				std::vector<float> colW(COLS);

				auto computeW = [&]() {
					for (int c = 0; c < COLS; ++c) {
						bool uc = (c == 0) ? false : (userCollapsedPhase[key_of(label[c])] == true);
						bool ac = autoCollapsed[c];
						colW[c] = (uc || ac) ? W_MIN[c] : W_FULL[c];
					}
				};
				computeW();

				float desired_w = total_table_width_fixed(colW, COLS);
				float avail_w = ImGui::GetContentRegionAvail().x;
				if (desired_w > avail_w) {
					for (int c = COLS - 1; c >= 1 && desired_w > avail_w; --c) {
						if (!userCollapsedPhase[key_of(label[c])]) {
							autoCollapsed[c] = true;
							computeW();
							desired_w = total_table_width_fixed(colW, COLS);
						}
					}
				}

				const ImGuiTableFlags tf =
					ImGuiTableFlags_SizingFixedFit |
					ImGuiTableFlags_RowBg |
					ImGuiTableFlags_BordersOuter |
					ImGuiTableFlags_Borders |
					ImGuiTableFlags_NoHostExtendX;

				if (ImGui::BeginTable("##phase_matrix", COLS, tf, ImVec2(desired_w, 0.0f))) {
					const ImGuiTableColumnFlags CF = ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize;

					for (int c = 0; c < COLS; ++c)
						ImGui::TableSetupColumn(label[c].c_str(), CF, colW[c]);

					ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
					for (int c = 0; c < COLS; ++c) {
						ImGui::TableSetColumnIndex(c);
						bool effCollapsed = (c == 0) ? false : (userCollapsedPhase[key_of(label[c])] == true);
						ImGui::PushStyleColor(ImGuiCol_Text, (effCollapsed || autoCollapsed[c])
							? ImVec4(1, 1, 1, 0.35f)
							: ImGui::GetStyleColorVec4(ImGuiCol_Text));
						ImGui::Selectable(label[c].c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
						bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
						ImGui::PopStyleColor();
						if (clicked && c != 0)
							userCollapsedPhase[key_of(label[c])] = !userCollapsedPhase[key_of(label[c])];
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip((effCollapsed || autoCollapsed[c]) ? "Click to expand" : "Click to collapse");
					}

					auto wrap_deg = [](double a)->double {
						if (!std::isfinite(a)) return a;
						a = std::fmod(a + 180.0, 360.0);
						if (a < 0) a += 360.0;
						return a - 180.0;
					};

					for (int i = 0; i < ROWS; ++i) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextColored(rows[i].color, "Ref: %s", rows[i].name.c_str());

						for (int j = 0; j < ROWS; ++j) {
							ImGui::TableSetColumnIndex(j + 1);
							double val;
							if (i == j) val = 0.0;
							else if (std::isfinite(phi[i]) && std::isfinite(phi[j]))
								val = wrap_deg(phi[j] - phi[i]);
							else
								val = std::numeric_limits<double>::quiet_NaN();
							ValueCellOrDash(val, "%.1f");
						}
					}

					ImGui::EndTable();
				}

				ImGui::Dummy(ImVec2(0, 2));
			}
		}


		ImGui::PopID();
	}






	

	void drawCursor(int id, double* cx, double* cy)
	{
		if (*cx == -1000 && *cy == -1000)
		{
			// Initial cursor centered
			ImPlotRect lim = ImPlot::GetPlotLimits();
			ImPlotPoint max = lim.Max();
			ImPlotPoint min = lim.Min();
			*cx = (max.x + min.x) / 2;
			*cy = (max.y + min.y) / 2;
		}

		ImPlot::DragPoint(id, cx, cy, ImVec4(1, 1, 1, 1), 8.0f);
		char cursor_label[20];
		std::string label = std::to_string(id) + ":";
		// Label is in (ms, V)
		snprintf(cursor_label, sizeof(cursor_label), (label + "(%.2f, %.2f)").c_str(), *cx, *cy);
		ImPlot::PlotText(cursor_label, *cx, *cy, ImVec2(50, -12));
		if (id == 2)
		{
			// Cursor 2 draws a dashed crosshair so the two cursors are
			// distinguishable at a glance
			ImDrawList* dl = ImPlot::GetPlotDrawList();
			const ImVec2 pos = ImPlot::GetPlotPos();
			const ImVec2 size = ImPlot::GetPlotSize();
			const ImVec2 px = ImPlot::PlotToPixels(*cx, *cy);
			const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.8f));
			const float dash = 8.0f, gap = 6.0f, weight = 2.5f;
			ImPlot::PushPlotClipRect();
			for (float y = pos.y; y < pos.y + size.y; y += dash + gap)
				dl->AddLine(ImVec2(px.x, y),
					ImVec2(px.x, ImMin(y + dash, pos.y + size.y)), col, weight);
			for (float x = pos.x; x < pos.x + size.x; x += dash + gap)
				dl->AddLine(ImVec2(x, px.y),
					ImVec2(ImMin(x + dash, pos.x + size.x), px.y), col, weight);
			ImPlot::PopPlotClipRect();
		}
		else
		{
			ImPlot::PlotInfLines((label + "vert").c_str(), cx, 1,
				ImPlotSpec(ImPlotProp_LineWeight, 2.5f, ImPlotProp_LineColor, ImVec4(1, 1, 1, 0.8), ImPlotProp_Flags, ImPlotInfLinesFlags_None));
			ImPlot::PlotInfLines((label + "vert").c_str(), cy, 1,
				ImPlotSpec(ImPlotProp_LineWeight, 2.5f, ImPlotProp_LineColor, ImVec4(1, 1, 1, 0.8), ImPlotProp_Flags, ImPlotInfLinesFlags_Horizontal));
		}
	}

	void UpdateOscData()
	{
		// Trace colours are theme-written; re-sync the caches every frame so
		// the plot follows a theme switch even while the panels are hidden.
		osc_control->refreshAccentCache();
		analysis_tools_widget->OSC1Colour = colourConvert(constants::OSC1_ACCENT);
		analysis_tools_widget->OSC2Colour = colourConvert(constants::OSC2_ACCENT);

		// sets whether the osc is paused or not (if paused, data will not update)
		OSC1Data->SetPaused(osc_control->Paused);
		OSC2Data->SetPaused(osc_control->Paused);
		// per-channel display transforms (probe attenuation + volt offset);
		// applied inside OscData wherever librador data is fetched, so plot,
		// trigger and analysis all see displayed volts (Qt isodriver parity)
		OSC1Data->SetDisplayTransform(osc_control->getAttenuationCH1(), osc_control->OffsetCH1);
		OSC2Data->SetDisplayTransform(osc_control->getAttenuationCH2(), osc_control->OffsetCH2);
		// sets the time that the trigger on the plot will trigger (basically the time where the trigger marker on the plot is; defaults at 0)
		OSC1Data->SetTriggerTimePlot(trigger_time_plot);
		OSC2Data->SetTriggerTimePlot(trigger_time_plot);
		// sets the entire vector which will be used to plot (including part cut off due to trigger)
		OSC1Data->SetExtendedData();
		OSC2Data->SetExtendedData();
		constants::Channel trigger_channel = maps::ComboItemToChannelTriggerPair.at(osc_control->TriggerTypeComboCurrentItem).channel;
		constants::TriggerType trigger_type = maps::ComboItemToChannelTriggerPair.at(osc_control->TriggerTypeComboCurrentItem).trigger_type;
		// calculates the time that the trigger occurs in the extended_data vector depending on which channel is triggering
		double trigger_time = 0;
		// sets trigger variable for each osc (this is a bit hacky but whatever)
		OSC1Data->SetTriggerOn(osc_control->Trigger);
		OSC2Data->SetTriggerOn(osc_control->Trigger);
		if (trigger_channel == constants::Channel::OSC1)
		{
			trigger_time = OSC1Data->GetTriggerTime(osc_control->Trigger, trigger_type,
				osc_control->TriggerLevel.getValue(), osc_control->TriggerHysteresis);
		}
		if (trigger_channel == constants::Channel::OSC2)
		{
			trigger_time = OSC2Data->GetTriggerTime(osc_control->Trigger, trigger_type,
				osc_control->TriggerLevel.getValue(), osc_control->TriggerHysteresis);
		}
		// sets the trigger time for both oscs
		OSC1Data->SetTriggerTime(trigger_time);
		OSC2Data->SetTriggerTime(trigger_time);
		// sets the data vector that will be plotted
		OSC1Data->SetData();
		OSC2Data->SetData();
		// sets the data vector used for analysis (FFT, period, Vpp etc; quite large)
		OSC1Data->SetRawData();
		OSC2Data->SetRawData();
		// sets another data vector used for analysis (contains a whole number of periods)
		OSC1Data->SetPeriodicData();
		OSC2Data->SetPeriodicData();
		// applies FFT to the raw_data vector
		OSC1Data->ApplyFFT();
		OSC2Data->ApplyFFT();
		// auto sets gain to maximise resolution without clipping
		AutoSetOscGain();
		// auto sets trigger level
		if (osc_control->AutoTriggerLevel)
		{
			AutoSetTriggerLevel(trigger_channel, trigger_type, &osc_control->TriggerLevel);
		}
	}
	void AutoSetTriggerLevel(constants::Channel trigger_channel,
	    constants::TriggerType trigger_type, SIValue* TriggerLevel)
	{
		double hysteresis_factor = 0.4;
		OscData* OSCData_ptr;
		if (trigger_channel == constants::Channel::OSC1)
		{
			OSCData_ptr = OSC1Data;
		}
		if (trigger_channel == constants::Channel::OSC2)
		{
			OSCData_ptr = OSC2Data;
		}
		TriggerLevel->setLevel(OSCData_ptr->GetAverage());
	}
	void DrawAndDragTriggers() {
		ImDrawList* draw_list = ImPlot::GetPlotDrawList();
		double trig_x = trigger_time_plot;
		double trig_y = osc_control->TriggerLevel.getValue();

		ImVec2 plot_min = ImPlot::GetPlotPos();
		ImVec2 plot_size = ImPlot::GetPlotSize();
		ImVec2 plot_max(plot_min.x + plot_size.x, plot_min.y + plot_size.y);
		ImPlotRect lim = ImPlot::GetPlotLimits();

		ImVec2 px = ImPlot::PlotToPixels(trig_x, trig_y);
		ImGuiIO& io = ImGui::GetIO();

		// trigger information
		constants::Channel trigger_channel = maps::ComboItemToChannelTriggerPair.at(osc_control->TriggerTypeComboCurrentItem).channel;
		constants::TriggerType trigger_type = maps::ComboItemToChannelTriggerPair.at(osc_control->TriggerTypeComboCurrentItem).trigger_type;

		ImColor col_trig_col = trigger_channel == constants::Channel::OSC1 ? osc_control->OSC1Colour : osc_control->OSC2Colour;
		if (!osc_control->Trigger)
		{
			col_trig_col.Value.w = 0.5f; // make transparent
		}
		ImU32 col_trig = ImGui::GetColorU32(col_trig_col.Value);

		// --- persistent per-frame drag state ---
		struct { bool x = false, y = false; } static drag;

		bool lmb_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
		bool lmb_dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left,1.0f);
		bool lmb_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
		bool double_clicked = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
		bool ctrl_down = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
		bool alt_down = ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt);
		bool shift_down = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);

		auto clampf = [](float v, float a, float b) { return v < a ? a : (v > b ? b : v); };

		// ---------------- TOP (X trigger) ----------------
		{
			float s = 10.0f;
			float x, y;
			bool on_screen = (px.x >= plot_min.x && px.x <= plot_max.x);
			bool scaled_font = false;

			if (on_screen) { x = px.x; y = plot_min.y; }
			else if (px.x < plot_min.x) { // left indicator
				float ss = 3.f * expf(100.f * float(trig_x - lim.X.Min)) + 5.f;
				s = ss; x = plot_min.x - s * 0.5f; y = plot_min.y;
				ImGui::SetWindowFontScale(s / 10.0f); scaled_font = true;
			}
			else {                      // right indicator
				float ss = 3.f * expf(100.f * float(lim.X.Max - trig_x)) + 5.f;
				s = ss; x = plot_max.x + s * 0.5f; y = plot_min.y;
				ImGui::SetWindowFontScale(s / 10.0f); scaled_font = true;
			}

			// Triangle + "T"
			ImVec2 p1(x, y + s), p2(x - 0.5f * s, y), p3(x + 0.5f * s, y);
			draw_list->AddTriangleFilled(p1, p2, p3, col_trig);
			ImVec2 ts = ImGui::CalcTextSize("T");
			draw_list->AddText(ImVec2(x - ts.x * 0.5f, y - ts.y + 4.f), col_trig, "T");

			


			// Hit box (larger than triangle)
			ImRect hit(ImVec2(x - 0.6f * s - 4.f, y - s - 4.f),
				ImVec2(x + 0.6f * s + 4.f, y + s + 4.f));
			bool hovered = hit.Contains(io.MousePos) && ImPlot::IsPlotHovered();
			if (hovered && !drag.y)
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
			}
			if (ctrl_down) DrawEdgeIndicatorX(draw_list, x, y, ts, trigger_type, col_trig);
			if (hovered && shift_down)
			{
				osc_control->Trigger = !osc_control->Trigger; // if ctrl + double clicked, toggle trigger
			}
			else if (hovered && double_clicked) // if x trigger left double clicked, set to 0s
			{
				trig_x = 0.0;
				next_resetX = false;
				next_resetY = false;
				next_autofitX = false;
				next_autofitY = false;
			}
			else if (hovered && lmb_clicked && ctrl_down)
			{
				ToggleTriggerTypeComboType(&osc_control->TriggerTypeComboCurrentItem);
			}
			else if (hovered && lmb_clicked && alt_down)
			{
				ToggleTriggerTypeComboChannel(&osc_control->TriggerTypeComboCurrentItem);
			}
			else if (hovered && lmb_clicked && !drag.y) drag.x = true;

			// Dragging
			if (drag.x && lmb_dragging) {
				float mx = clampf(io.MousePos.x, plot_min.x, plot_max.x);
				ImPlotPoint plot_xy = ImPlot::PixelsToPlot(ImVec2(mx, plot_min.y));
				trig_x = ImClamp(double(plot_xy.x), lim.X.Min, lim.X.Max);
			}
			if (drag.x && lmb_released) drag.x = false;

			if (scaled_font) ImGui::SetWindowFontScale(1.0f);
		}

		// ---------------- RIGHT (Y trigger) ----------------
		{
			float s = 10.0f;
			float x, y;
			bool on_screen = (px.y >= plot_min.y && px.y <= plot_max.y);
			bool scaled_font = false;

			if (on_screen) { x = plot_max.x; y = px.y; }
			else if (px.y > plot_max.y) { // bottom indicator
				float ss = 3.f * expf(1.f * float(trig_y - lim.Y.Min)) + 5.f;
				s = ss; x = plot_max.x; y = plot_max.y + s * 0.5f;
				ImGui::SetWindowFontScale(s / 10.0f); scaled_font = true;
			}
			else {                      // top indicator
				float ss = 3.f * expf(1.f * float(lim.Y.Max - trig_y)) + 5.f;
				s = ss; x = plot_max.x; y = plot_min.y - s * 0.5f;
				ImGui::SetWindowFontScale(s / 10.0f); scaled_font = true;
			}

			ImVec2 p1(x - s, y), p2(x, y - 0.5f * s), p3(x, y + 0.5f * s);
			draw_list->AddTriangleFilled(p1, p2, p3, col_trig);
			ImVec2 ts = ImGui::CalcTextSize("T");
			draw_list->AddText(ImVec2(x, y - ts.y * 0.5f), col_trig, "T");

			// Draw Automatic indicator
			if (osc_control->AutoTriggerLevel)
			{
				ImGui::SetWindowFontScale(s / 17.f); scaled_font = true;
				ImVec2 at_ts = ImGui::CalcTextSize("A");
				ImVec2 at_text_pos(x + 2.f, y - ts.y * 0.5f - at_ts.y+1.f);
				ImVec2 at_circle_pos(at_text_pos.x + 0.5 * at_ts.x - 0.5f, at_text_pos.y + 0.5 * at_ts.y + 0.5f);
				draw_list->AddText(ImVec2(at_text_pos.x, at_text_pos.y), col_trig, "A");
				draw_list->AddCircle(ImVec2(at_circle_pos.x, at_circle_pos.y), 0.4f * at_ts.y, col_trig);
			}

			ImRect hit(ImVec2(x - s - 4.f, y - s - 4.f),
				ImVec2(x + ts.x + 4.f, y + s + 4.f));
			bool hovered = hit.Contains(io.MousePos) && ImPlot::IsPlotHovered();
			if (hovered && !drag.x)
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
			}
			if (ctrl_down) DrawEdgeIndicatorY(draw_list, x, y, ts, trigger_type, col_trig);
			if (hovered && shift_down && lmb_clicked)
			{
				osc_control->Trigger = !osc_control->Trigger; // if ctrl + double clicked, toggle trigger
			}
			else if (hovered && double_clicked) // if y trigger left double clicked, toggel automode
			{
				osc_control->AutoTriggerLevel = !osc_control->AutoTriggerLevel;
				next_resetX = false;
				next_resetY = false;
				next_autofitX = false;
				next_autofitY = false;
			}
			else if (hovered && lmb_clicked && ctrl_down)
			{
				ToggleTriggerTypeComboType(&osc_control->TriggerTypeComboCurrentItem);
			}
			else if (hovered && lmb_clicked && alt_down)
			{
				ToggleTriggerTypeComboChannel(&osc_control->TriggerTypeComboCurrentItem);
			}
			else if (hovered && lmb_clicked && !drag.x) drag.y = true;
			if (drag.y && lmb_dragging) {
				osc_control->AutoTriggerLevel = false;
				float my = clampf(io.MousePos.y, plot_min.y, plot_max.y);
				ImPlotPoint plot_xy = ImPlot::PixelsToPlot(ImVec2(plot_min.x, my));
				trig_y = ImClamp(double(plot_xy.y), lim.Y.Min, lim.Y.Max);
			}
			if (drag.y && lmb_released) drag.y = false;

			if (scaled_font) ImGui::SetWindowFontScale(1.0f);
		}

		// Write back
		trigger_time_plot = trig_x;
		osc_control->TriggerLevel.setLevel(trig_y);

		// Publish current drag state so BeginPlot can apply flags next frame
		trig_dragging = drag.x || drag.y;
	}
	void DrawEdgeIndicatorX(ImDrawList* draw_list, float x, float y, ImVec2 ts, constants::TriggerType trigger_type, ImU32 col_trig)
	{
		// Draw Trigger Type indicator
		if (trigger_type == constants::TriggerType::RISING_EDGE)
		{
			ImVec2 tt_indicator_size(ts.x * 0.8, ts.y - 10.f);
			ImVec2 tt_indicator_pos(x - ts.x * 0.5f - tt_indicator_size.x + -4.0f, y - ts.y + 8.f);
			ImVec2 p1_line(tt_indicator_pos.x, tt_indicator_pos.y + tt_indicator_size.y),
				p2_line(tt_indicator_pos.x + tt_indicator_size.x * 0.5f, tt_indicator_pos.y + tt_indicator_size.y),
				p3_line(tt_indicator_pos.x + tt_indicator_size.x * 0.5f, tt_indicator_pos.y),
				p4_line(tt_indicator_pos.x + tt_indicator_size.x, tt_indicator_pos.y);
			draw_list->AddLine(p1_line, p2_line, col_trig, 1.0f);
			draw_list->AddLine(p2_line, p3_line, col_trig, 1.0f);
			draw_list->AddLine(p3_line, p4_line, col_trig, 1.0f);
		}
		else if (trigger_type == constants::TriggerType::FALLING_EDGE)
		{
			ImVec2 tt_indicator_size(ts.x * 0.8, ts.y - 10.f);
			ImVec2 tt_indicator_pos(x - ts.x * 0.5f - tt_indicator_size.x + -4.0f, y - ts.y + 8.f);
			ImVec2 p1_line(tt_indicator_pos.x, tt_indicator_pos.y),
				p2_line(tt_indicator_pos.x + tt_indicator_size.x * 0.5f, tt_indicator_pos.y),
				p3_line(tt_indicator_pos.x + tt_indicator_size.x * 0.5f, tt_indicator_pos.y + tt_indicator_size.y),
				p4_line(tt_indicator_pos.x + tt_indicator_size.x, tt_indicator_pos.y + tt_indicator_size.y);
			draw_list->AddLine(p1_line, p2_line, col_trig, 1.0f);
			draw_list->AddLine(p2_line, p3_line, col_trig, 1.0f);
			draw_list->AddLine(p3_line, p4_line, col_trig, 1.0f);
		}
	}
	void DrawEdgeIndicatorY(ImDrawList* draw_list, float x, float y, ImVec2 ts, constants::TriggerType trigger_type, ImU32 col_trig)
	{
		// Draw Trigger Type indicator
		ImVec2 tt_indicator_size(ts.x * 0.8, ts.y - 10.f);
		ImVec2 tt_indicator_pos(x + 1.0f, y + ts.y * 0.5f + 1.f);
		if (trigger_type == constants::TriggerType::RISING_EDGE)
		{
			ImVec2 p1_line(tt_indicator_pos.x, tt_indicator_pos.y + tt_indicator_size.y),
				p2_line(tt_indicator_pos.x + tt_indicator_size.x * 0.5f, tt_indicator_pos.y + tt_indicator_size.y),
				p3_line(tt_indicator_pos.x + tt_indicator_size.x * 0.5f, tt_indicator_pos.y),
				p4_line(tt_indicator_pos.x + tt_indicator_size.x, tt_indicator_pos.y);
			draw_list->AddLine(p1_line, p2_line, col_trig, 1.0f);
			draw_list->AddLine(p2_line, p3_line, col_trig, 1.0f);
			draw_list->AddLine(p3_line, p4_line, col_trig, 1.0f);
		}
		else if (trigger_type == constants::TriggerType::FALLING_EDGE)
		{
			ImVec2 p1_line(tt_indicator_pos.x, tt_indicator_pos.y),
				p2_line(tt_indicator_pos.x + tt_indicator_size.x * 0.5f, tt_indicator_pos.y),
				p3_line(tt_indicator_pos.x + tt_indicator_size.x * 0.5f, tt_indicator_pos.y + tt_indicator_size.y),
				p4_line(tt_indicator_pos.x + tt_indicator_size.x, tt_indicator_pos.y + tt_indicator_size.y);
			draw_list->AddLine(p1_line, p2_line, col_trig, 1.0f);
			draw_list->AddLine(p2_line, p3_line, col_trig, 1.0f);
			draw_list->AddLine(p3_line, p4_line, col_trig, 1.0f);
		}
	}

	// Draw a Combo when right clicking on the trigger (if there is time)
	void DrawTriggerCombo()
	{

	}

	

	void AutoSetOscGain()
	{
		int frame = ImGui::GetFrameCount();
		if (frame - last_update_frame < 5)
		{
			return;
		}

		int gainUpdate1 = 1;
		int gainUpdate2 = 1;

		// Check whether gain needs to increase or decrease
		if (osc_control->DisplayCheckOSC1)
		{
			gainUpdate1 = GetChangeToGain(OSC1Data->GetMiniBuffer());
		}
		
		if (osc_control->DisplayCheckOSC2)
		{
			gainUpdate2 = GetChangeToGain(OSC2Data->GetMiniBuffer());
		}
		if (osc_control->DisplayCheckOSC1 == false && osc_control->DisplayCheckOSC2 == false)
		{
			gainUpdate1 = 0;
			gainUpdate2 = 0;
		}

		bool changed = false;
		// Zoom out if either OSC data is clipping
		if (gainUpdate1 == -1 || gainUpdate2 == -1)
		{
			changed = true;
			currentLabOscGain--;
		}
		// Zoom in if both OSC datas are within threshold
		else if (gainUpdate1 == 1 && gainUpdate2 == 1)
		{
			changed = true;
			currentLabOscGain++;
		}

		if (changed)
		{
			librador_set_oscilloscope_gain(1 << currentLabOscGain);
			last_update_frame = frame;
#ifndef NDEBUG
			printf("Frame: %03d, gain: %02d\n", frame, 1 << currentLabOscGain);
#endif
		}
	}

	void renderControl() override
	{
		return; // no control
	}

	bool controlLab() override
	{
		return false; // no control
	}

private:
	const double adc_center = 3.3 / 2;
	const double gain_scale = 0.35;
	const double threshold_samples_frac = 0.1;
	
	int GetChangeToGain(
	    std::vector<double> osc_data)
	{
		// Check whether gain needs to decrease (zoom out, prevent clipping of larger waveforms)
		if (currentLabOscGain > 1)
		{
			const double offset = (1 << (6 - currentLabOscGain)) * gain_scale;
			const double max_thresh = adc_center + offset;
			const double min_thresh = adc_center - offset;

			// counts number of values which we identify to potentially be clipping
			int max_count = std::count_if(osc_data.begin(), osc_data.end(),
			    [max_thresh](double x) { return x > max_thresh; });
			int min_count = std::count_if(osc_data.begin(), osc_data.end(),
			    [min_thresh](double x) { return x < min_thresh; });
			if (max_count > threshold_samples_frac * osc_data.size()
			    || min_count > threshold_samples_frac * osc_data.size())
			{
				return -1; // decrement gain
			}
		}
		// Check if gain needs to increase (zoom in, for smaller waveforms)
		if (currentLabOscGain < 6)
		{
			const double offset = (1 << (5 - currentLabOscGain)) * gain_scale;
			const double max_thresh = adc_center + offset;
			const double min_thresh = adc_center - offset;
			const double grace = 0.2 * offset; // only apply to min game to prevent repetitive switching

			// counts number of values outside of the thresholds of the smaller gain
			int max_count = std::count_if(osc_data.begin(), osc_data.end(),
			    [max_thresh, grace](double x) { return x > max_thresh - grace; });
			int min_count = std::count_if(osc_data.begin(), osc_data.end(),
			    [min_thresh, grace](double x) { return x < min_thresh + grace; });
			if (max_count < threshold_samples_frac * osc_data.size()
			    && min_count < threshold_samples_frac * osc_data.size())
			{
				return 1; // increment gain
			}
		}
		
		return 0; // no change

	}

	
protected:
	ImVec2 size;
	bool paused = false;
	int prev_main_view = 0; // last frame's main-plot view (0 time, 1 XY, 2 eye)
	bool eye_fit_pending = false; // autofit the eye axes once sweeps exist
	bool trig_dragging = false;
	bool trig_double_clicked = false;
	bool next_resetX = false;
	bool next_resetY = false;
	bool next_autofitY = false;
	bool next_autofitX = false;
	double next_autofitX_min = 0;
	double next_autofitX_max = 0;
	double next_autofitY_min = 0;
	double next_autofitY_max = 0;
	double init_time_range_lower = -0.015;
	double init_time_range_upper = 0.015;
	double init_voltage_range_lower = -1.0;
	double init_voltage_range_upper = 5.0;
	// Eye view opening window, from Qt's display3 (isodriver.h: ±1 ms, ±12 V)
	double init_eye_time_range_lower = -0.001;
	double init_eye_time_range_upper = 0.001;
	double init_eye_voltage_range_lower = -12.0;
	double init_eye_voltage_range_upper = 12.0;
	OSCControl* osc_control;
	AnalysisToolsWidget* analysis_tools_widget;
	double cursor1_x = -1000;
	double cursor1_y = -1000;
	double cursor2_x = -1000;
	double cursor2_y = -1000;
	double trigger_time_plot = 0;
	int last_update_frame = 10;
	// Spectrum Stuff
	double init_freq_range_lower = 1e-3;
	double init_freq_range_upper = 375000. / 2;
	struct AxisLimitRanges {
		double init_lower;
		double init_upper;
		double constraint_lower;
		double constraint_upper;
	};
	AxisLimitRanges magnitude_db_range = { -100, 0, -300, 300 };
	AxisLimitRanges magnitude_VRMS_range = { 0, 1, 0, 20 };
	bool spectrum_autofit = false;
	bool spectrum_was_off = true;
	bool network_was_off = true;
	
	PlotTrace DecimateLogPlotTrace(const std::vector<double>& x,
		const std::vector<double>& y)
	{
		PlotTrace out;
		const size_t N = x.size();
		if (N == 0 || N != y.size()) return out;

		// Plot geometry
		const ImPlotRect lim = ImPlot::GetPlotLimits();
		double xmin = std::max(lim.X.Min, 1e-300);
		double xmax = std::max(lim.X.Max, xmin * 1.0001);
		if (!(xmin < xmax)) return out;

		const ImVec2 size = ImPlot::GetPlotSize();
		const int W = (int)size.x;
		if (W <= 1) return out;

		// Indices that bound the visible X-range
		const size_t i_lo = (size_t)(std::lower_bound(x.begin(), x.end(), xmin) - x.begin());
		const size_t i_hi_ub = (size_t)(std::upper_bound(x.begin(), x.end(), xmax) - x.begin()); // one past last in-range
		if (i_lo >= i_hi_ub) {
			// No points in view; optionally include nearest neighbors for continuity
			if (i_lo > 0) { out.x.push_back(x[i_lo - 1]); out.y.push_back(y[i_lo - 1]); }
			if (i_lo < N) { out.x.push_back(x[std::min(i_lo, N - 1)]); out.y.push_back(y[std::min(i_lo, N - 1)]); }
			return out;
		}

		// If only a handful of points are in range, just copy them (no decimation)
		const size_t in_view_count = i_hi_ub - i_lo;
		if (in_view_count <= (size_t)std::max(4, W / 8)) {
			// include left guard if exists
			if (i_lo > 0) { out.x.push_back(x[i_lo - 1]); out.y.push_back(y[i_lo - 1]); }
			for (size_t k = i_lo; k < i_hi_ub; ++k) {
				if (!out.x.empty() && x[k] == out.x.back() && y[k] == out.y.back()) continue;
				out.x.push_back(x[k]); out.y.push_back(y[k]);
			}
			// include right guard if exists
			if (i_hi_ub < N) { out.x.push_back(x[i_hi_ub]); out.y.push_back(y[i_hi_ub]); }
			return out;
		}

		// Build log-space bin edges over the *visible* range
		const double Lmin = std::log(xmin);
		const double Lmax = std::log(xmax);
		const double dL = (Lmax - Lmin) / W;

		std::vector<double> edge_f(W + 1);
		for (int c = 0; c <= W; ++c)
			edge_f[c] = std::exp(Lmin + c * dL);

		out.x.reserve((size_t)W * 2);
		out.y.reserve((size_t)W * 2);

		auto push = [&](size_t idx) {
			if (!out.x.empty() && x[idx] == out.x.back() && y[idx] == out.y.back()) return;
			out.x.push_back(x[idx]); out.y.push_back(y[idx]);
		};

		// Left guard: include the sample just before the first in-range point to avoid left cutoff
		if (i_lo > 0) push(i_lo - 1);

		size_t i = i_lo;
		bool pushed_any = false;
		for (int c = 0; c < W; ++c) {
			// clamp search to in-view upper bound
			size_t j = (size_t)(std::lower_bound(x.begin() + i, x.begin() + i_hi_ub, edge_f[c + 1]) - x.begin());
			if (i >= j) continue; // no points in this column

			pushed_any = true;

			const size_t i_first = i;
			const size_t i_last = j - 1;

			// min / max within [i, j)
			size_t i_min = i_first, i_max = i_first;
			double ymin = y[i_first], ymax = y[i_first];
			for (size_t k = i_first + 1; k < j; ++k) {
				if (y[k] < ymin) { ymin = y[k]; i_min = k; }
				if (y[k] > ymax) { ymax = y[k]; i_max = k; }
			}

			// First, extrema, last (classic min-max decimation)
			push(i_first);
			if (i_min != i_first && i_min != i_last) push(i_min);
			if (i_max != i_first && i_max != i_last && i_max != i_min) push(i_max);
			if (i_last != i_first) push(i_last);

			i = j; // advance to next bin
		}

		// If nothing got pushed from bins (e.g., extremely sparse), copy the in-view span
		if (!pushed_any) {
			for (size_t k = i_lo; k < i_hi_ub; ++k) push(k);
		}

		// Right guard: include the first sample *after* the in-range block to avoid right cutoff
		if (i_hi_ub < N) push(i_hi_ub);

		return out;
	}

	// Network Analyser Stuff
	NetworkAnalyser* na = nullptr;
	NetworkAnalyser::Config* na_cfg = nullptr;

	// Put near your UI file top
	static inline void FormatHzSI(double f, char* buf, int n) {
		if (f >= 1e9) std::snprintf(buf, n, "%.3g GHz", f / 1e9);
		else if (f >= 1e6) std::snprintf(buf, n, "%.3g MHz", f / 1e6);
		else if (f >= 1e3) std::snprintf(buf, n, "%.3g kHz", f / 1e3);
		else              std::snprintf(buf, n, "%.3g Hz", f);
	}

	// Build + apply adaptive ticks from *current* view without locking setup.
	// Requires: called between BeginPlot/EndPlot, before any Plot* calls.
	inline void ApplyAdaptiveLogXTicks_Internal(ImAxis axis_x) {
		ImPlotContext* gp = GImPlot;
		IM_ASSERT(gp && gp->CurrentPlot);
		ImPlotPlot* plot = gp->CurrentPlot;

		// read current visible range directly (no SetupLock)
		ImPlotAxis& ax = plot->Axes[axis_x];
		const double xmin = ax.Range.Min;
		const double xmax = ax.Range.Max;
		if (!(xmin > 0.0) || !(xmax > xmin)) return;

		std::vector<double> ticks;
		ticks.reserve(64);

		// any 10^k in view?
		const int k_lo = (int)std::ceil(std::log10(xmin));
		const int k_hi = (int)std::floor(std::log10(xmax));
		const bool has_major = (k_lo <= k_hi);

		if (has_major) {
			for (int k = k_lo; k <= k_hi; ++k) {
				const double v = std::pow(10.0, (double)k);
				if (v >= xmin && v <= xmax) ticks.push_back(v); // 10, 100, 1k, ...
			}
		}
		else {
			// no majors → show in-decade ticks; super tight → linear fallback
			const double log_span = std::log10(xmax) - std::log10(xmin);
			if (log_span < 0.15) {
				// VERY tight: linear 5 ticks so there’s always labels
				const int N = 5;
				const double step = (xmax - xmin) / (N - 1);
				for (int i = 0; i < N; ++i) ticks.push_back(xmin + i * step);
			}
			else if (log_span <= 1.05) {
				// within ~one decade
				const int    kd = (int)std::floor(std::log10(0.5 * (xmin + xmax)));
				const double base = std::pow(10.0, (double)kd);
				const bool   dense = (log_span <= 0.5);
				if (dense) {
					for (int m = 1; m <= 9; ++m) {
						const double v = m * base;
						if (v >= xmin && v <= xmax) ticks.push_back(v); // 1..9
					}
				}
				else {
					static const double M[] = { 1,2,5 };
					for (double m : M) {
						const double v = m * base;
						if (v >= xmin && v <= xmax) ticks.push_back(v); // 1,2,5
					}
				}
			}
			else {
				// multi-decade span but edges exclude majors → coarse 1,2,5 per decade
				const int kmin = (int)std::floor(std::log10(xmin));
				const int kmax = (int)std::ceil(std::log10(xmax));
				static const double M[] = { 1,2,5 };
				for (int k = kmin; k <= kmax; ++k) {
					const double base = std::pow(10.0, (double)k);
					for (double m : M) {
						const double v = m * base;
						if (v >= xmin && v <= xmax) ticks.push_back(v);
					}
				}
			}
		}

		if (!ticks.empty())
			ImPlot::SetupAxisTicks(axis_x, ticks.data(), (int)ticks.size(), /*labels*/ nullptr);
	}

	// Estimate how tall the Signal Properties + (optional) Phase tables will be.
	// Uses ImGui metrics so it matches your font/spacing.
	// Note: we assume both collapsing headers are OPEN when toggled on.
	// If you want to be conservative, keep `include_phase = true`.
	static float EstimateSignalPropsHeight(int signal_count, bool include_phase) {
		const ImGuiStyle& st = ImGui::GetStyle();
		const float row_h = ImGui::GetTextLineHeightWithSpacing();
		const float hdr_row = ImGui::GetFrameHeightWithSpacing(); // collapsing header height

		// --- Signal Properties table (header + rows) ---
		const int sig_rows = std::max(1, signal_count); // at least 1 empty row
		const float sig_table_h =
			// header row + column headers + rows
			(row_h /*table header row*/)+
			(sig_rows * row_h) +
			st.CellPadding.y * 2.0f + st.FrameBorderSize;

		// --- Phase Difference table (header + N rows) ---
		float phase_table_h = 0.0f;
		if (include_phase && signal_count >= 2) {
			const int phase_rows = signal_count; // one row per reference
			phase_table_h =
				(row_h /*table header row*/)+
				(phase_rows * row_h) +
				st.CellPadding.y * 2.0f + st.FrameBorderSize;
		}

		// Collapsing headers + a touch of spacing between tables
		float total =
			hdr_row + sig_table_h + st.ItemSpacing.y +
			(include_phase ? (hdr_row + phase_table_h + st.ItemSpacing.y) : 0.0f);

		// small breathing room so the bottom border stays visible
		total += 2.0f;
		return total;
	}

	
};