#pragma once
#include "ControlWidget.hpp"
#include "util.h"
#include "platform/file_dialog.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

/// <summary>
/// DAQ file replay widget (Qt parity: actionOpen_DAQ_File + daqloadprompt +
/// isoDriver::loadFileBuffer/enableFileMode/fileTimerTick, with the
/// daqLoad_startChanged/endChanged start/end trim).
///
/// Loads a previously-recorded DAQ text file back into memory and lets the user
/// scrub / trim it on this widget's OWN ImPlot plot, instead of pushing it back
/// through the live-acquisition path (deliberately isolated so replay never
/// entangles USB streaming — the Qt app reused isoDriver for this; we do not).
///
/// ON-DISK FORMAT — matches the recorder EXACTLY. See librador
/// usbcallhandler.cpp:
///   * drive_daq()        (usbcallhandler.cpp:417) writes, for "both channels",
///     CH A then a bare "\n" (line 437) then CH B.
///   * daq_for_channel()  (usbcallhandler.cpp:375) writes, per channel:
///       - a header line: SDL_IOprintf(iostream, "%s\n", ch_names[channel-1])
///         with ch_names[2] = {"CH A", "CH B"} (line 395-396), then
///       - all samples on ONE line, each value followed by a single space:
///         Volts -> "%.3g " ("%.4g " in multimeter mode 6/7)  (line 401-405)
///         Bits  -> "%.0f " (line 397-400)
///         ADC   -> "%.0f " (line 406-413)
///     Samples are written oldest-first (chronological order — the Volts/Bits
///     loops iterate rbegin()->rend() over the buffer, ADC walks forward).
///
/// So a two-channel file is literally:
///     CH A
///     v0 v1 v2 ... vN
///     CH B
///     w0 w1 w2 ... wM
/// and a single-channel file is just the first two of those lines. There is no
/// sample-rate / averaging / mode header in this format (unlike the legacy Qt
/// QCustomPlot CSV that isoDriver::loadFileBuffer parsed), so time on the x-axis
/// is reconstructed from a user-supplied sample rate; the default x-axis is the
/// raw sample index.
/// </summary>
class DAQReplay : public ControlWidget
{
public:
	DAQReplay(std::string label, ImVec2 size, const float* accentColour)
	    : ControlWidget(label, size, accentColour)
	{}

	/// <summary>Open the load-file dialog (async native dialog; callback runs
	/// on the main thread during a later frame via PumpFileDialogResults).
	/// Public so the desktop File menu can trigger it directly.</summary>
	void openFilePrompt()
	{
		if (dialog_open)
			return;
		dialog_open = true;
		ShowOpenFileDialog("txt", [this](const char* path) {
			dialog_open = false;
			if (!path)
				return; // user cancelled
			loadFile(path);
		});
	}

	/// <summary>Render UI: open button, trim controls, and the replay plot.</summary>
	void renderControl() override
	{
		ImGui::Dummy(ImVec2(0.0f, 5.0f));

		ImGui::BeginDisabled(dialog_open);
		if (ImGui::Button("Open DAQ file...##daqreplay"))
		{
			openFilePrompt();
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (loaded_path.empty())
			ImGui::TextColored(constants::GRAY_TEXT, "No file loaded");
		else
			ImGui::TextColored(constants::GRAY_TEXT, "%s", loaded_basename.c_str());

		// Loud failure surfaced to the user (parser throws -> caught -> shown)
		if (!error_message.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s",
			    error_message.c_str());
			return;
		}

		if (!have_a && !have_b)
			return;

		const int total = maxSampleCount();

		// --- Axis: sample index (default) or reconstructed time ---
		ImGui::Checkbox("Time axis##daqreplay", &use_time_axis);
		if (use_time_axis)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ScaledPx(120.0f));
			ImGui::InputFloat("kSa/s##daqreplay_rate", &sample_rate_kSa, 0.0f, 0.0f,
			    "%.4g");
			if (sample_rate_kSa <= 0.0f)
				sample_rate_kSa = 375.0f; // avoid divide-by-zero / meaningless axis
		}
		const double x_scale = xScale();

		// --- Start/end trim (Qt daqLoad_startChanged/endChanged). Trim is kept
		// in SAMPLE space so it is invariant to the index<->time axis toggle. ---
		ImGui::SetNextItemWidth(ScaledPx(140.0f));
		ImGui::DragInt("Start##daqreplay", &trim_start, 1.0f, 0, total - 1, "%d");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ScaledPx(140.0f));
		ImGui::DragInt("End##daqreplay", &trim_end, 1.0f, 0, total, "%d");
		clampTrim(total);

		if (use_time_axis)
			ImGui::TextColored(constants::GRAY_TEXT,
			    "Showing samples %d-%d (%.4g s) of %d", trim_start, trim_end,
			    (trim_end - trim_start) * x_scale, total);
		else
			ImGui::TextColored(constants::GRAY_TEXT,
			    "Showing samples %d-%d of %d", trim_start, trim_end, total);

		if (ImGui::Button("Fit to selection##daqreplay"))
			fit_requested = true;
		ImGui::SameLine();
		if (ImGui::Button("Reset trim##daqreplay"))
		{
			trim_start = 0;
			trim_end = total;
			fit_requested = true;
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(dialog_open || (trim_end - trim_start) <= 0);
		if (ImGui::Button("Export trimmed CSV...##daqreplay"))
			exportTrimmed();
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (saved_flash > 0.0f)
		{
			saved_flash -= ImGui::GetIO().DeltaTime;
			ImGui::TextUnformatted("Saved");
		}

		// --- The replay plot (this widget's own, isolated from the scope) ---
		ImPlotFlags plot_flags = ImPlotFlags_NoMenus;
		if (ImPlot::BeginPlot("##DAQReplayPlot", ImVec2(-1, 240), plot_flags))
		{
			const char* y_label = channelsAreVolts() ? "Volts" : "";
			ImPlot::SetupAxes(use_time_axis ? "Time (s)" : "Sample",
			    y_label, ImPlotAxisFlags_NoLabel, 0);
			if (use_time_axis)
				ImPlot::SetupAxisFormat(ImAxis_X1, MetricFormatter, (void*)"s");

			// One-shot zoom to the trimmed range ("replot the trimmed range")
			if (fit_requested)
			{
				ImPlot::SetupAxisLimits(ImAxis_X1, trim_start * x_scale,
				    trim_end * x_scale, ImPlotCond_Always);
				fit_requested = false;
			}

			plotChannel(ch_a, have_a, "CH A", constants::OSC1_ACCENT, x_scale, total);
			plotChannel(ch_b, have_b, "CH B", constants::OSC2_ACCENT, x_scale, total);

			// Draggable trim boundaries on the plot (Qt's start/end handles).
			// Kept in sample space: convert to x-units, drag, convert back.
			double x_start = trim_start * x_scale;
			double x_end = trim_end * x_scale;
			bool changed = false;
			changed |= ImPlot::DragLineX(0, &x_start, ImVec4(1, 1, 1, 0.9f), 2.0f);
			changed |= ImPlot::DragLineX(1, &x_end, ImVec4(1, 1, 1, 0.9f), 2.0f);
			if (changed)
			{
				trim_start = (int)std::lround(x_start / x_scale);
				trim_end = (int)std::lround(x_end / x_scale);
				clampTrim(total);
			}

			ImPlot::EndPlot();
		}
	}

	/// <summary>Pure viewer/parser — nothing to push to the board.</summary>
	bool controlLab() override { return true; }

private:
	/// <summary>Parse a DAQ text file into per-channel sample vectors. Throws
	/// std::runtime_error on any malformed input (loud failure).</summary>
	static void parseFile(const char* path, std::vector<double>& out_a,
	    std::vector<double>& out_b, bool& out_have_a, bool& out_have_b)
	{
		std::ifstream f(path);
		if (!f)
			throw std::runtime_error(
			    std::string("Could not open file: ") + path);

		out_a.clear();
		out_b.clear();
		out_have_a = false;
		out_have_b = false;

		std::vector<double>* target = nullptr; // current channel being filled
		std::string line;
		int lineno = 0;
		while (std::getline(f, line))
		{
			lineno++;
			// Trim leading/trailing whitespace (incl. a trailing '\r' from
			// Windows-saved files, and the recorder's trailing space).
			size_t b = line.find_first_not_of(" \t\r\n");
			if (b == std::string::npos)
				continue; // blank line
			size_t e = line.find_last_not_of(" \t\r\n");
			std::string trimmed = line.substr(b, e - b + 1);

			if (trimmed == "CH A")
			{
				target = &out_a;
				out_have_a = true;
				continue;
			}
			if (trimmed == "CH B")
			{
				target = &out_b;
				out_have_b = true;
				continue;
			}

			// A numeric data line must follow a channel header.
			if (target == nullptr)
				throw std::runtime_error(
				    "Malformed DAQ file: data before any \"CH A\"/\"CH B\" "
				    "header (line "
				    + std::to_string(lineno) + ")");

			std::istringstream iss(trimmed);
			double val;
			while (iss >> val)
				target->push_back(val);
			// A leftover non-numeric token means the file is not a DAQ dump.
			if (!iss.eof())
			{
				iss.clear();
				std::string bad;
				iss >> bad;
				throw std::runtime_error(
				    "Malformed DAQ file: expected a number but found \"" + bad
				    + "\" (line " + std::to_string(lineno) + ")");
			}
		}

		if (!out_have_a && !out_have_b)
			throw std::runtime_error("Not a DAQ file: no \"CH A\"/\"CH B\" "
			                         "header found");
		if (out_a.empty() && out_b.empty())
			throw std::runtime_error("DAQ file contains no samples");
	}

	void loadFile(const char* path)
	{
		try
		{
			parseFile(path, ch_a, ch_b, have_a, have_b);
		}
		catch (const std::exception& ex)
		{
			// Loud-but-recoverable: show the error line to the user (the
			// documented "throw or show an error line" outcome for a
			// user-selected bad file) and drop any partial data.
			error_message = ex.what();
			ch_a.clear();
			ch_b.clear();
			have_a = false;
			have_b = false;
			loaded_path.clear();
			loaded_basename.clear();
			return;
		}

		error_message.clear();
		loaded_path = path;
		loaded_basename = basename(loaded_path);
		trim_start = 0;
		trim_end = maxSampleCount();
		fit_requested = true;
	}

	void plotChannel(const std::vector<double>& v, bool present, const char* name,
	    const float accent[3], double x_scale, int total)
	{
		(void)total;
		if (!present || v.empty())
			return;
		ImVec4 bright(accent[0], accent[1], accent[2], 1.0f);
		ImVec4 dim(accent[0], accent[1], accent[2], 0.22f);

		// Full trace, dimmed, for context (no legend/fit — the bright trimmed
		// trace owns those).
		ImPlot::PlotLine((std::string("##full_") + name).c_str(), v.data(),
		    (int)v.size(), x_scale, 0.0,
		    ImPlotSpec(ImPlotProp_LineWeight, 1.5f, ImPlotProp_LineColor, dim,
		        ImPlotProp_Flags, ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit));

		// Bright overlay for the trimmed [start,end) range only.
		int s = ImMin(trim_start, (int)v.size());
		int e = ImMin(trim_end, (int)v.size());
		int count = e - s;
		if (count > 0)
			ImPlot::PlotLine(name, v.data() + s, count, x_scale, s * x_scale,
			    ImPlotSpec(ImPlotProp_LineWeight, 2.0f, ImPlotProp_LineColor, bright));
	}

	void exportTrimmed()
	{
		dialog_open = true;
		ShowSaveFileDialog("csv", [this](const char* path) {
			dialog_open = false;
			if (!path)
				return; // cancelled
			std::string out = path;
			if (out.size() < 4 || out.compare(out.size() - 4, 4, ".csv") != 0)
				out += ".csv";

			std::ofstream f(out);
			if (!f)
			{
				error_message = std::string("Could not write: ") + out;
				return;
			}

			// Header + one row per trimmed sample; blank where a channel is
			// absent or shorter than the trim window.
			f << (use_time_axis ? "Time (s)" : "Sample");
			if (have_a)
				f << ",CH A";
			if (have_b)
				f << ",CH B";
			f << "\n";

			const double x_scale = xScale();
			for (int i = trim_start; i < trim_end; i++)
			{
				if (use_time_axis)
					f << (i * x_scale);
				else
					f << i;
				if (have_a)
				{
					f << ",";
					if (i < (int)ch_a.size())
						f << ch_a[i];
				}
				if (have_b)
				{
					f << ",";
					if (i < (int)ch_b.size())
						f << ch_b[i];
				}
				f << "\n";
			}
			saved_flash = 1.5f;
		});
	}

	int maxSampleCount() const
	{
		return ImMax((int)ch_a.size(), (int)ch_b.size());
	}

	// A channel recorded in Volts stays within a few tens of volts; ADC/Bits
	// dumps are large integer codes. We can't know the units from the file, so
	// only label the y-axis "Volts" when the data plausibly is volts.
	bool channelsAreVolts() const
	{
		double m = 0.0;
		for (double x : ch_a)
			m = ImMax(m, x < 0 ? -x : x);
		for (double x : ch_b)
			m = ImMax(m, x < 0 ? -x : x);
		return m <= 100.0;
	}

	double xScale() const
	{
		return use_time_axis ? 1.0 / ((double)sample_rate_kSa * 1000.0) : 1.0;
	}

	void clampTrim(int total)
	{
		if (total < 1)
			total = 1;
		trim_start = ImClamp(trim_start, 0, total - 1);
		trim_end = ImClamp(trim_end, trim_start + 1, total);
	}

	static std::string basename(const std::string& p)
	{
		size_t slash = p.find_last_of("/\\");
		return slash == std::string::npos ? p : p.substr(slash + 1);
	}

	// Loaded data (per channel, chronological order)
	std::vector<double> ch_a;
	std::vector<double> ch_b;
	bool have_a = false;
	bool have_b = false;

	std::string loaded_path;
	std::string loaded_basename;
	std::string error_message;

	// Trim window in SAMPLE indices: [trim_start, trim_end)
	int trim_start = 0;
	int trim_end = 0;

	bool use_time_axis = false;
	float sample_rate_kSa = 375.0f; // recorder's base ADC rate; user-adjustable
	bool fit_requested = false;
	bool dialog_open = false;
	float saved_flash = 0.0f;
};
