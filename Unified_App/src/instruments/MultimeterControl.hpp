#pragma once
#include "ControlWidget.hpp"
#include "InputsControl.hpp"
#include "librador.h"
#include "util.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

/// <summary>Multimeter widget — V / I / R / C measurements over device mode 7
/// (12-bit sampling of the DUT+/DUT- pins). Active only while the Inputs
/// widget has the device in Multimeter mode. The ADC-code → volts conversion
/// happens inside librador (o1buffer::sampleConvert with
/// twelve_bit_multimeter, plus the MULTIMETER_INVERT sign fix at ingest), so
/// the samples read here are already multimeter volts in the Qt app's display
/// convention. Measurement math ported from Desktop_Interface/isodriver.cpp
/// (multimeterAction / multimeterStats / meanVoltageLast) and isobuffer.cpp
/// (capSample).
/// </summary>
class MultimeterControl : public ControlWidget
{
public:
	// Values match the Qt multimeterType_enum (isodriver.h)
	enum class MeterMode : int
	{
		V = 0,
		I = 1,
		R = 2,
		C = 3,
	};

	MultimeterControl(std::string label, ImVec2 size, const float* accentColour)
	    : ControlWidget(label, size, accentColour)
	{}

	/// <summary>
	/// The inputs widget owns the device mode; the multimeter only measures
	/// while it is set to Multimeter. App must call this before the first
	/// Render().
	/// </summary>
	void SetInputs(InputsControl* inputs_widget)
	{
		inputs = inputs_widget;
	}

	/// <summary>
	/// Render UI elements for the multimeter
	/// </summary>
	void renderControl() override
	{
		IM_ASSERT(inputs != nullptr); // App must call SetInputs() before rendering

		ImGui::Dummy(ImVec2(0.0f, 5.0f));

		if (inputs->mode() != InputsControl::Mode::Multimeter)
		{
			ImGui::TextColored(constants::GRAY_TEXT,
			    "Set Inputs mode to Multimeter to use the multimeter.");
			return;
		}

		if (!librador_iso_thread_is_active())
		{
			ImGui::TextColored(constants::GRAY_TEXT,
			    "Connect a Labrador board to measure.");
		}

		// Measurement type (Qt multimeterModeSelect)
		ImGui::Text("Measure");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ScaledPx(140.0f));
		if (ImGui::BeginCombo("##mm_mode", mode_labels[(int)meter_mode]))
		{
			for (int i = 0; i < (int)IM_ARRAYSIZE(mode_labels); i++)
			{
				bool is_selected = (i == (int)meter_mode);
				if (ImGui::Selectable(mode_labels[i], is_selected))
				{
					setMeterMode((MeterMode)i);
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		// Unit forcing (Qt's per-quantity Range menus: Auto / mV / V, ...)
		ImGui::SameLine();
		ImGui::Text("Range");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ScaledPx(80.0f));
		int* range = &range_sel[(int)meter_mode];
		if (ImGui::BeginCombo(
		        "##mm_range", range_labels[(int)meter_mode][*range]))
		{
			for (int i = 0; i < 3; i++)
			{
				bool is_selected = (i == *range);
				if (ImGui::Selectable(range_labels[(int)meter_mode][i], is_selected))
				{
					*range = i;
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		// Series resistance (Qt multimeterResistanceSelect, 0 - 1 MOhm): the
		// shunt for I, the divider reference for R, the charge resistor for C
		if (meter_mode != MeterMode::V)
		{
			ImGui::Text("Series R");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ScaledPx(120.0f));
			ImGui::InputDouble(
			    "##mm_series_r", &series_resistance, 0.0, 0.0, u8"%.0f Ω");
			series_resistance = ImClamp(series_resistance, 0.0, 1000000.0);
		}

		// R drive source (Qt multimeterRComboBox → isoDriver::rSourceChanged;
		// the estimator's Vin is (source * 2) + 3 volts)
		if (meter_mode == MeterMode::R)
		{
			ImGui::Text("Source");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ScaledPx(140.0f));
			if (ImGui::BeginCombo("##mm_rsource", rsource_labels[rsource_idx]))
			{
				for (int i = 0; i < (int)IM_ARRAYSIZE(rsource_labels); i++)
				{
					bool is_selected = (i == rsource_idx);
					if (ImGui::Selectable(rsource_labels[i], is_selected))
					{
						rsource_idx = i;
						drive_dirty = true;
					}
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			if (rsource_idx == 0)
			{
				ImGui::TextColored(constants::GRAY_TEXT,
				    "Signal Gen CH2 is driven at 3.0 V DC.\n"
				    "Wire: CH2 - series R - unknown R - GND,\n"
				    "probe the middle junction with DUT+.");
			}
			else
			{
				// The PSU widget re-sends its own voltage periodically, so the
				// supply cannot be commandeered from here (Qt forces its PSU
				// slider to 5 V instead — mainwindow.cpp rSourceIndexChanged)
				ImGui::TextColored(constants::GRAY_TEXT,
				    "Set the Power Supply widget to 5.0 V.\n"
				    "Wire: PSU - series R - unknown R - GND,\n"
				    "probe the middle junction with DUT+.");
			}
		}

		if (meter_mode == MeterMode::C)
		{
			ImGui::TextColored(constants::GRAY_TEXT,
			    "Signal Gen CH2 drives a 4 Hz square wave (0 - 3 V).\n"
			    "Wire: CH2 - series R - capacitor - GND,\n"
			    "probe the middle junction with DUT+.");
		}

		// Pause freezes the mode-7 buffer (readings hold their last value)
		ImGui::Checkbox("Pause##mm", &paused_request);

		// Primary readout
		ImGui::Dummy(ImVec2(0.0f, 5.0f));
		ImGui::TextColored(
		    constants::GRAY_TEXT, "%s", prim_captions[(int)meter_mode]);
		ImGui::PushFont(NULL, ImGui::GetFontSize() * 2.0f); // 1.92: size-only push
		if (prim_valid)
		{
			ImGui::Text("%.4g %s", prim_val, prim_unit);
		}
		else
		{
			ImGui::TextDisabled("----");
		}
		ImGui::PopFont();

		// Max/Min/Mean/RMS grid (Qt shows the four LCDs for V and I; R and C
		// publish a single value)
		if ((meter_mode == MeterMode::V || meter_mode == MeterMode::I)
		    && stats_valid)
		{
			if (ImGui::BeginTable("##mm_stats", 4, ImGuiTableFlags_SizingStretchSame))
			{
				for (int i = 0; i < 4; i++)
				{
					ImGui::TableNextColumn();
					ImGui::TextColored(constants::GRAY_TEXT, "%s (%s)",
					    stat_names[i], stat_unit[i]);
					ImGui::Text("%.4g", stat_val[i]);
				}
				ImGui::EndTable();
			}
		}
	}

	/// <summary>
	/// Push pause / source-signal state to the board and take readings at the
	/// Qt LCD cadence. Returns false on disconnected-type librador errors
	/// (the caller keeps running, like PSUControl).
	/// </summary>
	bool controlLab() override
	{
		bool active = (inputs != nullptr)
		    && (inputs->mode() == InputsControl::Mode::Multimeter);
		if (!active)
		{
			// Qt's rSource(255) path re-enables the SG/PSU panels without
			// touching their outputs, so there is no drive signal to undo —
			// but release our pause hold so the scope's CH1 is not frozen
			if (was_active && paused_applied)
			{
				paused_applied = false;
				paused_request = false;
				if (librador_set_paused(1, false))
				{
					return false; // board vanished mid-switch
				}
			}
			was_active = false;
			return true;
		}

		if (!was_active)
		{
			was_active = true;
			drive_dirty = true;
			stats_elapsed_s = kStatsPeriod_s; // publish on the first tick
			cap_warmup_s = kCapWindow_s;
			prim_valid = false;
			stats_valid = false;
			estimated_resistance = 0.0;
		}

		// Push the pause state on change only
		if (paused_request != paused_applied)
		{
			int error = librador_set_paused(1, paused_request);
			if (error)
			{
#ifndef NDEBUG
				printf("librador_set_paused FAILED with error code %d\n", error);
#endif
				return false;
			}
			paused_applied = paused_request;
		}

		// Source-signal upkeep (Qt MainWindow::rSourceIndexChanged +
		// isoDriver::setMultimeterType):
		//   R + SG source → CH2 held at 3 V DC (Qt loads DC.tlw, amplitude 3)
		//   C             → CH2 4 Hz 0-3 V square (charge/discharge drive)
		//   V / I / R + PSU source → nothing of ours to send (see the PSU
		//   hint in renderControl)
		// Note: the SG widget only re-sends CH2 when the user touches it, so
		// this holds until then — proper CH2-sharing arbitration is the later
		// "rSource 253/254/255" milestone (docs/PLAN.md).
		if (inputs->modeChangedThisFrame())
		{
			drive_dirty = true;
		}
		if (drive_dirty && librador_iso_thread_is_active())
		{
			int error = 0;
			if (meter_mode == MeterMode::R && rsource_idx == 0)
			{
				unsigned char dc_wave[2] = { 255, 255 };
				error = librador_update_signal_gen_settings(
				    2, dc_wave, 2, 1000.0, kRSourceSGVolts, 0.0);
			}
			else if (meter_mode == MeterMode::C)
			{
				error = librador_send_square_wave(
				    2, kCapDriveFreq_Hz, kCapDriveAmplitude, 0.0);
			}
			if (error)
			{
#ifndef NDEBUG
				printf("multimeter source setup FAILED with error code %d\n",
				    error);
#endif
				return false;
			}
			drive_dirty = false;
		}

		// Measurement refresh at the Qt LCD cadence (MULTIMETER_PERIOD).
		// While paused the buffer serves a frozen snapshot, so the reading
		// simply holds (same net behaviour as Qt's paused_multimeter).
		float dt = ImGui::GetIO().DeltaTime;
		cap_warmup_s = (meter_mode == MeterMode::C && librador_iso_thread_is_active())
		    ? std::max(0.0f, cap_warmup_s - dt)
		    : kCapWindow_s;
		stats_elapsed_s += dt;
		if (stats_elapsed_s >= kStatsPeriod_s)
		{
			stats_elapsed_s = 0.0f;
			refreshMeasurement();
		}
		return true;
	}

private:
	// Unit forcing states, indices into range_labels[mode]
	enum RangeSel : int
	{
		RangeAuto = 0,
		RangeSmall = 1, // mV / mA / Ohm / nF
		RangeBig = 2,   // V / A / kOhm / uF
	};

	void setMeterMode(MeterMode m)
	{
		if (m == meter_mode)
			return;
		meter_mode = m;
		drive_dirty = true; // Qt setMultimeterType reconfigures the source
		prim_valid = false;
		stats_valid = false;
		cap_warmup_s = kCapWindow_s;
		estimated_resistance = 0.0; // restart the iterative estimate
	}

	/// <summary>
	/// Take a fresh reading and publish the display values. Ported from
	/// isoDriver::multimeterStats plus analogConvert's stat accumulation.
	/// A null/empty return from librador means no samples yet (device just
	/// connected or the mode just changed) — the display shows "----".
	/// </summary>
	void refreshMeasurement()
	{
		// Mode-7 samples are already multimeter volts (librador o1buffer
		// converts the 12-bit codes and applies MULTIMETER_INVERT internally)
		std::vector<double>* window = librador_get_analog_data_by_rate(
		    1, kStatsWindow_s, kStatsRate_Hz, 0, 0);
		if (window == nullptr || window->empty())
		{
			prim_valid = false;
			stats_valid = false;
			return;
		}

		// Vmax/Vmin/Vmean/VRMS accumulation (isodriver.cpp analogConvert)
		double vmax = -20.0, vmin = 20.0; // Qt's seed values
		double accumulated = 0.0, accumulated_square = 0.0;
		for (double s : *window)
		{
			accumulated += s;
			accumulated_square += s * s;
			if (s > vmax) vmax = s;
			if (s < vmin) vmin = s;
		}
		const double n = (double)window->size();
		const double vmean = accumulated / n;
		const double vrms = std::sqrt(accumulated_square / n);
		// NOTE: `window` points at librador's shared conversion vector and is
		// invalidated by the further librador_get_analog_data_by_rate calls
		// below — only the scalars above may be used from here on.

		switch (meter_mode)
		{
		case MeterMode::V:
		case MeterMode::I:
		{
			// V: autorange each stat to mV when |V| < 1 (per-stat, exactly as
			// Qt's mvMax/mvMin/mvMean/mvRMS); I = V / seriesR, mA when |I| < 1
			const double raw[4] = { vmax, vmin, vmean, vrms };
			const bool amps = (meter_mode == MeterMode::I);
			const int range = range_sel[(int)meter_mode];
			for (int i = 0; i < 4; i++)
			{
				double value = amps ? raw[i] / series_resistance : raw[i];
				bool small_unit = (range == RangeAuto)
				    ? (std::fabs(value) < 1.0)
				    : (range == RangeSmall);
				stat_val[i] = small_unit ? value * 1000.0 : value;
				stat_unit[i] = range_labels[(int)meter_mode]
				                           [small_unit ? RangeSmall : RangeBig];
			}
			stats_valid = true;
			prim_val = stat_val[2]; // Mean
			prim_unit = stat_unit[2];
			prim_valid = true;
			break;
		}
		case MeterMode::R:
			refreshResistance();
			break;
		case MeterMode::C:
			refreshCapacitance();
			break;
		}
	}

	/// <summary>
	/// One iteration of the Qt resistance estimator (isodriver.cpp
	/// multimeterStats, R branch). DUT+ probes the junction of the series
	/// (reference) resistor and the unknown resistance:
	///   Vin - [series R] - x - [unknown R] - GND
	/// with the frontend's R3+R4 divider loading the node (the ch2_ref
	/// perturbation term). The estimate feeds back into that loading
	/// correction, so it converges over a few refresh ticks.
	/// </summary>
	void refreshResistance()
	{
		// Qt resets a NaN estimate before iterating
		if (std::isnan(estimated_resistance))
			estimated_resistance = 0.0;

		// Vm = mean over the last MULTIMETER_PERIOD, 1024 points — exactly
		// isoDriver::meanVoltageLast(0.5, 1, 2048)
		std::vector<double>* window = librador_get_analog_data_by_rate(
		    1, kRMeanWindow_s, kRMeanRate_Hz, 0, 0);
		if (window == nullptr || window->empty())
		{
			prim_valid = false;
			return;
		}
		double sum = 0.0;
		for (double s : *window)
			sum += s;
		double Vm = sum / (double)window->size();

		double rtest_para_r
		    = 1.0 / (1.0 / series_resistance + 1.0 / estimated_resistance);
		double perturbation
		    = kCh2Ref * (rtest_para_r / (kR3 + kR4 + rtest_para_r));
		Vm = Vm - perturbation;
		double Vin = (double)(rsource_idx * 2) + 3.0; // SG = 3 V, PSU = 5 V
		double Vrat = (Vin - Vm) / Vin;
		double Rp = 1.0 / (1.0 / series_resistance + 1.0 / (kR3 + kR4));
		// Perturbation term on V2 ignored (Qt comment): V1 = Vin,
		// V2 = Vin(Rp/(R+Rp)) + Vn(Rtest||R / (R34 + Rtest||R34))
		estimated_resistance = ((1.0 - Vrat) / Vrat) * Rp;

		bool kilo = (range_sel[(int)MeterMode::R] == RangeAuto)
		    ? (estimated_resistance > 1000.0)
		    : (range_sel[(int)MeterMode::R] == RangeBig);
		prim_val = kilo ? estimated_resistance / 1000.0 : estimated_resistance;
		prim_unit
		    = range_labels[(int)MeterMode::R][kilo ? RangeBig : RangeSmall];
		prim_valid = true;
	}

	/// <summary>
	/// RC time-constant capacitance measurement (isodriver.cpp multimeterStats
	/// C branch + isoBuffer::capSample). CH2 squares between 0 and 3 V; the
	/// scan finds where DUT+ sits below 0.8 V (x0), where it charges back
	/// above 0.8 V (x1), then above 2.5 V (x2):
	///   C = -dt / (R * ln((vcc - 2.5) / (vcc - 0.8))),  dt = (x2 - x1) / rate
	/// A failed scan keeps the previous reading (Qt returns early too).
	/// </summary>
	void refreshCapacitance()
	{
		if (cap_warmup_s > 0.0f)
			return; // buffer does not hold kCapWindow_s of mode-7 data yet

		std::vector<double>* window = librador_get_analog_data_by_rate(
		    1, kCapWindow_s, kSampleRate_Hz, 0, 0);
		if (window == nullptr || window->empty())
			return;

		// Samples arrive newest-first (o1buffer getMany_double walks back
		// from the write head) — the same indexing as Qt's bufferAt()
		const int samples = (int)window->size();
		int x0 = capSample(*window, samples, 0, kCapVbot, true);
		if (x0 == -1)
			return;
		int x1 = capSample(*window, samples, -x0, kCapVbot, false);
		if (x1 == -1)
			return;
		int x2 = capSample(*window, samples, -x1, kCapVtop, false);
		if (x2 == -1)
			return;

		double dt = (double)(x2 - x1) / kSampleRate_Hz;
		double Cm = -dt
		    / (series_resistance
		        * std::log((kVcc - kCapVtop) / (kVcc - kCapVbot)));

		bool micro = (range_sel[(int)MeterMode::C] == RangeAuto)
		    ? (Cm > 1e-6)
		    : (range_sel[(int)MeterMode::C] == RangeBig);
		prim_val = micro ? Cm * 1e6 : Cm * 1e9;
		prim_unit
		    = range_labels[(int)MeterMode::C][micro ? RangeBig : RangeSmall];
		prim_valid = true;
	}

	/// <summary>
	/// Port of isoBuffer::capSample: scan the window oldest → newest for a
	/// run of kSamplesSeekingCap samples beyond `threshold` (below it when
	/// seek_below, above it otherwise; the run counter decays on misses).
	/// v is newest-first — v[i] is i samples behind the write head, exactly
	/// Qt's bufferAt(i). offset < 0 skips the first -offset positions of the
	/// window. Returns the position from the start of the window; -1 means
	/// "not found", an expected outcome the caller handles by keeping the
	/// previous reading.
	/// </summary>
	static int capSample(const std::vector<double>& v, int samples, int offset,
	    double threshold, bool seek_below)
	{
		if ((int)v.size() < samples + offset)
			return -1;

		int found = 0;
		for (int i = samples + offset; i--;)
		{
			bool beyond = seek_below ? (v[i] < threshold) : (v[i] > threshold);
			if (beyond)
				found = found + 1;
			else
				found = std::max(0, found - 1);

			if (found > kSamplesSeekingCap)
				return samples - i;
		}
		return -1;
	}

	// Hardware constants (Desktop_Interface xmega.h / isodriver.h)
	static constexpr double kVcc = 3.3;      // xmega.h vcc
	static constexpr double kR3 = 1000000.0; // frontend divider, xmega.h R3
	static constexpr double kR4 = 75000.0;   // frontend divider, xmega.h R4
	static constexpr double kCh2Ref = 1.65;  // frontend bias, isodriver.h ch2_ref
	static constexpr double kSampleRate_Hz = 375000.0; // mode-7 buffer rate

	// Qt measurement parameters
	static constexpr float kStatsPeriod_s = 0.5f;   // MULTIMETER_PERIOD (500 ms)
	static constexpr double kStatsWindow_s = 0.1;   // V/I stats window
	static constexpr double kStatsRate_Hz = 3750.0; // 375 samples per window
	static constexpr double kRMeanWindow_s = 0.5;   // meanVoltageLast window
	static constexpr double kRMeanRate_Hz = 2048.0; // 1024 points, as Qt
	static constexpr double kRSourceSGVolts = 3.0;  // SG CH2 DC level in R mode
	static constexpr double kCapDriveFreq_Hz = 4.0; // C-mode square wave
	static constexpr double kCapDriveAmplitude = 3.0; // 0 - 3 V square
	static constexpr float kCapWindow_s = 1.0f;     // cap scan window (Qt: 1 s)
	static constexpr double kCapVbot = 0.8;         // isodriver.cpp cap_vbot
	static constexpr double kCapVtop = 2.5;         // isodriver.cpp cap_vtop
	static constexpr int kSamplesSeekingCap = 20;   // isobuffer.cpp hysteresis

	static constexpr const char* mode_labels[4]
	    = { "Voltage", "Current", "Resistance", "Capacitance" };
	static constexpr const char* prim_captions[4]
	    = { "Mean", "Mean", "Resistance", "Capacitance" };
	static constexpr const char* stat_names[4] = { "Max", "Min", "Mean", "RMS" };
	static constexpr const char* rsource_labels[2]
	    = { "Signal Gen CH2", "Power Supply" };
	// [mode][RangeAuto / RangeSmall / RangeBig]
	static constexpr const char* range_labels[4][3] = {
		{ "Auto", "mV", "V" },
		{ "Auto", "mA", "A" },
		{ "Auto", u8"Ω", u8"kΩ" },
		{ "Auto", "nF", u8"μF" },
	};

	InputsControl* inputs = nullptr;

	// UI state
	MeterMode meter_mode = MeterMode::V;
	int range_sel[4] = { RangeAuto, RangeAuto, RangeAuto, RangeAuto };
	// Qt's multimeterResistanceSelect defaults to 0 Ohm, which reads "inf" in
	// current mode until the user types their shunt in — start somewhere sane
	double series_resistance = 100.0;
	int rsource_idx = 0; // 0 = SG CH2, 1 = PSU (isoDriver multimeterRsource)
	bool paused_request = false;

	// Hardware-facing latches
	bool was_active = false;
	bool paused_applied = false;
	bool drive_dirty = false;

	// Published readings (refreshed at the Qt LCD cadence)
	bool prim_valid = false;
	double prim_val = 0.0;
	const char* prim_unit = "";
	bool stats_valid = false;
	double stat_val[4] = { 0.0, 0.0, 0.0, 0.0 };
	const char* stat_unit[4] = { "", "", "", "" };
	double estimated_resistance = 0.0; // estimator feedback (Qt member)
	float stats_elapsed_s = 0.0f;
	float cap_warmup_s = 0.0f;
};
