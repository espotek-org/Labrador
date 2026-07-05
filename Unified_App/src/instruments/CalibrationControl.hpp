#pragma once
#include "ControlWidget.hpp"
#include "librador.h"
#include "util.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

/// <summary>Oscilloscope + PSU calibration wizard, ported from the Qt app's
/// 3-stage flow (Desktop_Interface/mainwindow.cpp on_actionCalibrate_triggered,
/// calibrateStage2/3 and calibrate_psu_stage2/3).
///
/// Scope calibration: with everything disconnected each channel floats at the
/// frontend bias (~1.65 V); measuring it gives the per-channel vref
/// (CalibrateVrefCHx). With both channels grounded (USB shield) the residual
/// reading gives the frontend gain correction (CalibrateGainCHx). Corrections
/// are applied inside librador's sample->voltage conversion via
/// librador_set_channel_calibration. PSU calibration: measure the PSU output
/// on CH1 at 5 V (hardware gain 4) and 10 V (gain 1); the mean error becomes
/// a voltage offset applied by librador_set_psu_calibration_offset.
///
/// Persistence is the App's job: persist vref(1/2), gainScale(1/2) when
/// calibrationValid(), and psuOffset() when psuOffsetValid(); restore at
/// startup via applyStored / applyStoredPsuOffset.
///
/// App integration requirements while wizardActive():
///   - do NOT run PSUControl::controlLab (it would overwrite the 5 V / 10 V
///     calibration setpoints, Qt equivalent: psuSlider forced to 0), and do
///     NOT run auto-gain or re-apply the OSC gain selection (the PSU stages
///     own the hardware gain, Qt equivalent: setAutoGain(false)).
///   - the wizard switches the device mode itself (dual scope for the scope
///     stages, CH1-only for the PSU stages, mirroring Qt's workspace change);
///     after the wizard the App should re-assert InputsControl's mode (e.g.
///     InputsControl::markDirty()).
/// </summary>
class CalibrationControl : public ControlWidget
{
public:
	CalibrationControl(std::string label, ImVec2 size, const float* accentColour)
	    : ControlWidget(label, size, accentColour)
	{}

	// ------------------------------------------------------------------
	// Persistence API (used by the App's settings store)
	// ------------------------------------------------------------------

	/// <summary>True once a scope calibration is active (completed wizard or
	/// applyStored). The App should persist vref/gainScale only when true.
	/// </summary>
	bool calibrationValid() const { return scope_valid; }

	/// <summary>Measured channel bias voltage, Qt CalibrateVrefCHx semantics
	/// (neutral 1.65). ch is 1 or 2.</summary>
	double vref(int ch) const
	{
		IM_ASSERT(ch == 1 || ch == 2);
		return vref_ch[ch - 1];
	}

	/// <summary>Gain correction as a multiplier on the nominal R4/(R3+R4)
	/// frontend gain (neutral 1.0). Qt persists the absolute gain
	/// (CalibrateGainCHx); that equals gainScale(ch) * R4/(R3+R4).
	/// ch is 1 or 2.</summary>
	double gainScale(int ch) const
	{
		IM_ASSERT(ch == 1 || ch == 2);
		return gain_scale_ch[ch - 1];
	}

	/// <summary>Restore a persisted scope calibration (both channels) and mark
	/// it valid. Safe to call before librador_init / before the board is
	/// connected — values are re-applied from controlLab until librador
	/// accepts them.</summary>
	void applyStored(double vref1, double gain1, double vref2, double gain2)
	{
		vref_ch[0] = vref1;
		gain_scale_ch[0] = gain1;
		vref_ch[1] = vref2;
		gain_scale_ch[1] = gain2;
		scope_valid = true;
		needs_apply = !applyScopeCalToLibrador();
	}

	/// <summary>True once a PSU calibration is active (completed wizard or
	/// applyStoredPsuOffset).</summary>
	bool psuOffsetValid() const { return psu_valid; }

	/// <summary>PSU offset in volts, Qt CalibratePsu semantics (neutral 0).
	/// </summary>
	double psuOffset() const { return psu_offset; }

	/// <summary>Restore a persisted PSU calibration offset. Same
	/// apply-when-possible behaviour as applyStored.</summary>
	void applyStoredPsuOffset(double offset)
	{
		psu_offset = offset;
		psu_valid = true;
		needs_apply |= (librador_set_psu_calibration_offset(psu_offset) != 0);
	}

	/// <summary>Persist the current calibration into the board's EEPROM
	/// (librador vendor requests, firmware >= 0x000A).  Called after each
	/// completed wizard stage; safe no-op when nothing is valid yet.
	/// Non-fatal on failure - the local Settings copy remains the
	/// authoritative mirror (a DFU erase wipes the device copy).</summary>
	void saveToDevice()
	{
		if (!scope_valid && !psu_valid)
			return;
		librador_save_calibration_to_device(vref_ch[0], gain_scale_ch[0],
			vref_ch[1], gain_scale_ch[1], psu_offset);
	}

	/// <summary>Fetch calibration from the board's EEPROM and apply it
	/// (device copy wins over local settings - it travels with the
	/// hardware).  Returns true if a valid stored calibration was loaded.
	/// Call once per connection.</summary>
	bool loadFromDevice()
	{
		double v1, g1, v2, g2, po;
		if (librador_load_calibration_from_device(&v1, &g1, &v2, &g2, &po) != 0)
			return false;
		applyStored(v1, g1, v2, g2);
		applyStoredPsuOffset(po);
		return true;
	}

	/// <summary>True while the wizard is running (see the class comment for
	/// what the App must suppress during that time).</summary>
	bool wizardActive() const { return stage != Stage::Idle && stage != Stage::Failed; }

	/// <summary>
	/// Render instructions/buttons for the current wizard stage.
	/// </summary>
	void renderControl() override
	{
		ImGui::Dummy(ImVec2(0.0f, 5.0f));

		bool connected = librador_is_connected();

		// controlLab only runs while connected; drop any request raised just
		// before a disconnect so the wizard cannot start by surprise later.
		if (!connected)
		{
			start_scope_requested = false;
			start_psu_requested = false;
			advance_requested = false;
		}

		// Qt closes its calibration dialogs on the driver's killMe signal;
		// here a mid-wizard disconnect aborts and restores the previous state.
		if (wizardActive() && stage != Stage::ScopeDone && stage != Stage::PsuDone
		    && !connected)
		{
			abortForDisconnect();
		}

		switch (stage)
		{
		case Stage::Idle:
			renderStatusSummary();
			ImGui::Dummy(ImVec2(0.0f, 5.0f));
			if (!connected)
				ImGui::TextColored(constants::GRAY_TEXT,
				    "Connect a Labrador board to calibrate.");
			ImGui::BeginDisabled(!connected);
			if (ImGui::Button("Calibrate Oscilloscope##cal_start_scope"))
				start_scope_requested = true;
			ImGui::SameLine();
			if (ImGui::Button("Calibrate PSU##cal_start_psu"))
				start_psu_requested = true;
			ImGui::EndDisabled();
			break;

		case Stage::ScopeDisconnectPrompt:
			// Qt: "Please disconnect all wires from your Labrador board then
			// press OK to continue."
			ImGui::TextWrapped("Step 1 of 2: Please disconnect everything from "
			                   "CH1 and CH2 (all wires off the Labrador board), "
			                   "then press Next.");
			renderNextCancel();
			break;

		case Stage::ScopeVrefSettle:
			renderMeasuringProgress(kScopeSettleSeconds);
			break;

		case Stage::ScopeGroundPrompt:
			// Qt: "Please connect both oscilloscope channels to the outer
			// shield of the USB connector then press OK to continue."
			ImGui::TextWrapped("Step 2 of 2: Please connect both CH1 and CH2 to "
			                   "ground (the outer shield of the USB connector), "
			                   "then press Next.");
			renderNextCancel();
			break;

		case Stage::ScopeGainSettle:
			renderMeasuringProgress(kScopeSettleSeconds);
			break;

		case Stage::ScopeDone:
			ImGui::TextWrapped("Oscilloscope calibration complete.");
			renderStatusSummary();
			ImGui::Dummy(ImVec2(0.0f, 5.0f));
			ImGui::BeginDisabled(!connected);
			if (ImGui::Button("Calibrate PSU##cal_chain_psu"))
				start_psu_requested = true;
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Done##cal_scope_done"))
				stage = Stage::Idle;
			break;

		case Stage::PsuConnectPrompt:
			// Qt: "Please connect your Labrador's Oscilloscope CH1 (DC) pin to
			// the Power Supply Output (positive) then press OK to continue."
			ImGui::TextWrapped("Please connect the Oscilloscope CH1 (DC) pin to "
			                   "the Power Supply output (positive), then press "
			                   "Next.");
			renderNextCancel();
			break;

		case Stage::Psu5Settle:
		case Stage::Psu10Settle:
			renderMeasuringProgress(kPsuSettleSeconds);
			break;

		case Stage::PsuDone:
		{
			std::string done_text = "PSU calibration complete.  Offset = "
			    + formatVolts(psu_offset) + ".";
			ImGui::TextWrapped("%s", done_text.c_str());
			if (ImGui::Button("Done##cal_psu_done"))
				stage = Stage::Idle;
			break;
		}

		case Stage::Failed:
			ImGui::TextWrapped("%s", error_text.c_str());
			if (ImGui::Button("OK##cal_failed_ok"))
				stage = Stage::Idle;
			break;
		}
	}

	/// <summary>
	/// Advance the wizard state machine and talk to the board. Called by the
	/// App every frame while the board is connected (all librador side
	/// effects live here; renderControl only raises request flags).
	/// </summary>
	bool controlLab() override
	{
		// Re-push restored calibration once librador is ready (applyStored may
		// run before librador_init at app startup).
		if (needs_apply)
		{
			bool ok = applyScopeCalToLibrador();
			ok = (librador_set_psu_calibration_offset(psu_offset) == 0) && ok;
			needs_apply = !ok;
		}

		if (cancel_requested)
		{
			cancel_requested = false;
			cancelWizard();
		}
		if (start_scope_requested)
		{
			start_scope_requested = false;
			startScopeWizard();
		}
		if (start_psu_requested)
		{
			start_psu_requested = false;
			startPsuWizard();
		}

		switch (stage)
		{
		case Stage::ScopeDisconnectPrompt:
			if (consumeAdvance())
			{
				// Qt: clearBuffers + QTimer::singleShot(1200, calibrateStage2)
				settle_deadline = ImGui::GetTime() + kScopeSettleSeconds;
				stage = Stage::ScopeVrefSettle;
			}
			break;

		case Stage::ScopeVrefSettle:
			if (ImGui::GetTime() >= settle_deadline)
				measureScopeVref();
			break;

		case Stage::ScopeGroundPrompt:
			if (consumeAdvance())
			{
				// Qt: clearBuffers + QTimer::singleShot(1200, calibrateStage3)
				settle_deadline = ImGui::GetTime() + kScopeSettleSeconds;
				stage = Stage::ScopeGainSettle;
			}
			break;

		case Stage::ScopeGainSettle:
			if (ImGui::GetTime() >= settle_deadline)
				measureScopeGain();
			break;

		case Stage::PsuConnectPrompt:
			if (consumeAdvance())
			{
				// Qt: driver->setPsu(5); clearBuffers;
				// QTimer::singleShot(1800, calibrate_psu_stage2)
				librador_set_power_supply_voltage(5);
				settle_deadline = ImGui::GetTime() + kPsuSettleSeconds;
				stage = Stage::Psu5Settle;
			}
			break;

		case Stage::Psu5Settle:
			if (ImGui::GetTime() >= settle_deadline)
				measurePsu5();
			break;

		case Stage::Psu10Settle:
			if (ImGui::GetTime() >= settle_deadline)
				measurePsu10();
			break;

		default:
			break;
		}

		advance_requested = false; // never let a stray Next leak across stages
		return true;
	}

private:
	// Qt settle times: QTimer::singleShot(1200/1800 ms) between workspace
	// changes and measurements (mainwindow.cpp calibrate* slots).
	static constexpr double kScopeSettleSeconds = 1.2;
	static constexpr double kPsuSettleSeconds = 1.8;
	static constexpr double kNeutralVref = 1.65;
	static constexpr double kNeutralGainScale = 1.0;
	// Hardware device modes (see usbcallhandler.cpp isoCallback):
	static constexpr int kModeDualScope = 2; // scope cal: CH1 + CH2 analog
	static constexpr int kModeCh1Scope = 0;  // PSU cal: CH1 analog only

	enum class Stage
	{
		Idle,
		ScopeDisconnectPrompt, // wait for user: everything unplugged
		ScopeVrefSettle,       // settle 1.2 s then measure channel bias
		ScopeGroundPrompt,     // wait for user: CH1+CH2 to USB shield
		ScopeGainSettle,       // settle 1.2 s then measure gain correction
		ScopeDone,
		PsuConnectPrompt,      // wait for user: CH1 to PSU output
		Psu5Settle,            // PSU 5 V, gain 4, settle 1.8 s, measure
		Psu10Settle,           // PSU 10 V, gain 1, settle 1.8 s, measure
		PsuDone,
		Failed,
	};

	Stage stage = Stage::Idle;
	double settle_deadline = 0.0;
	std::string error_text;

	// Active calibration (what vref()/gainScale()/psuOffset() report)
	double vref_ch[2] = { kNeutralVref, kNeutralVref };
	double gain_scale_ch[2] = { kNeutralGainScale, kNeutralGainScale };
	bool scope_valid = false;
	double psu_offset = 0.0;
	bool psu_valid = false;
	bool needs_apply = false;

	// Pre-wizard snapshot, restored on Cancel/failure/disconnect. (Deviation
	// from Qt, which persists the neutral values it resets to at wizard start;
	// restoring the previous calibration is strictly safer.)
	double prev_vref_ch[2] = { kNeutralVref, kNeutralVref };
	double prev_gain_scale_ch[2] = { kNeutralGainScale, kNeutralGainScale };
	bool prev_scope_valid = false;
	double prev_psu_offset = 0.0;
	bool prev_psu_valid = false;
	double prev_scope_gain = 1.0; // hardware gain before the PSU stages

	// UI -> controlLab requests (all librador traffic happens in controlLab)
	bool advance_requested = false;
	bool cancel_requested = false;
	bool start_scope_requested = false;
	bool start_psu_requested = false;

	// PSU wizard intermediate (Qt PSU5)
	double psu5_measured = 0.0;

	bool consumeAdvance()
	{
		bool a = advance_requested;
		advance_requested = false;
		return a;
	}

	// ------------------------------------------------------------------
	// Measurement + math (ported from Qt)
	// ------------------------------------------------------------------

	/// <summary>Qt isoDriver::meanVoltageLast(1, ch, 128): mean of the last
	/// second of converted samples (a 1024-sample read of the channel's
	/// buffer). librador_get_analog_data_by_rate with a 1 s window at 1024 Hz
	/// reproduces it on the unified buffers (TOP=128 is applied inside
	/// o1buffer::sampleConvert for scope modes). Returns false if no data is
	/// available (e.g. board disconnected).</summary>
	static bool meanVoltageLastSecond(int channel, double* out_mean)
	{
		std::vector<double>* data
		    = librador_get_analog_data_by_rate(channel, 1.0, 1024.0, 0.0, 0);
		if (!data || data->empty())
			return false;
		double sum = 0.0;
		for (double v : *data)
			sum += v;
		*out_mean = sum / (double)data->size();
		return true;
	}

	bool applyScopeCalToLibrador()
	{
		int e1 = librador_set_channel_calibration(1, vref_ch[0], gain_scale_ch[0]);
		int e2 = librador_set_channel_calibration(2, vref_ch[1], gain_scale_ch[1]);
		return e1 == 0 && e2 == 0;
	}

	void snapshotScope()
	{
		prev_vref_ch[0] = vref_ch[0];
		prev_vref_ch[1] = vref_ch[1];
		prev_gain_scale_ch[0] = gain_scale_ch[0];
		prev_gain_scale_ch[1] = gain_scale_ch[1];
		prev_scope_valid = scope_valid;
	}

	void restoreScopeSnapshot()
	{
		vref_ch[0] = prev_vref_ch[0];
		vref_ch[1] = prev_vref_ch[1];
		gain_scale_ch[0] = prev_gain_scale_ch[0];
		gain_scale_ch[1] = prev_gain_scale_ch[1];
		scope_valid = prev_scope_valid;
		needs_apply = !applyScopeCalToLibrador();
	}

	/// <summary>Qt on_actionCalibrate_triggered: change workspace to dual
	/// scope, throw out the old calibration, ask the user to unplug
	/// everything.</summary>
	void startScopeWizard()
	{
		snapshotScope();

		// Qt resets ch refs to 1.65 and both frontend gains to R4/(R3+R4)
		// before measuring, so stage 2/3 read with neutral corrections.
		vref_ch[0] = kNeutralVref;
		vref_ch[1] = kNeutralVref;
		gain_scale_ch[0] = kNeutralGainScale;
		gain_scale_ch[1] = kNeutralGainScale;
		scope_valid = false;
		if (!applyScopeCalToLibrador())
		{
			restoreScopeSnapshot();
			fail("Calibration could not start (librador rejected the neutral "
			     "calibration).  Please try again.");
			return;
		}

		// Qt's workspace change: everything off except scope CH1 + CH2, DC
		// coupled, no pause, no trigger.
		if (librador_set_device_mode(kModeDualScope) != 0)
		{
			restoreScopeSnapshot();
			fail("Calibration could not start (failed to switch the board to "
			     "dual-oscilloscope mode).  Please try again.");
			return;
		}
		neutraliseChannelView(1);
		neutraliseChannelView(2);

		stage = Stage::ScopeDisconnectPrompt;
	}

	/// <summary>Qt calibrateStage2: with everything unplugged both channels
	/// read the frontend bias. Valid range (1.1, 2.1) V; the measured value
	/// becomes CalibrateVrefCHx and 3.3 - v enters the conversion.</summary>
	void measureScopeVref()
	{
		double v1, v2;
		if (!meanVoltageLastSecond(1, &v1) || !meanVoltageLastSecond(2, &v2))
		{
			restoreScopeSnapshot();
			fail("Calibration has been abandoned: no oscilloscope data was "
			     "available.  Please check the connection and try again.");
			return;
		}

		// Qt: if((vref > 2.1) | (vref < 1.1)) abandon
		if (v1 > 2.1 || v1 < 1.1 || v2 > 2.1 || v2 < 1.1)
		{
			restoreScopeSnapshot();
			fail("Calibration has been abandoned due to out-of-range values.  "
			     "Both channels should show approximately 1.6 V.  Please "
			     "disconnect all wires from your Labrador board and try again.");
			return;
		}

		vref_ch[0] = v1;
		vref_ch[1] = v2;
		// Qt applies ch_ref = 3.3 - vref immediately (gain still neutral), so
		// the stage-3 measurement below is taken with the corrected bias.
		if (!applyScopeCalToLibrador())
		{
			restoreScopeSnapshot();
			fail("Calibration has been abandoned: librador rejected the "
			     "measured reference values.  Please try again.");
			return;
		}

		stage = Stage::ScopeGroundPrompt;
	}

	/// <summary>Qt calibrateStage3: with both channels grounded the residual
	/// reading vM (valid within +/-0.3 V) yields the gain correction
	/// G' = (ref - vM) * G / ref  with ref = 3.3 - vref.  With G neutral that
	/// is gain_scale = (ref - vM) / ref.
	/// (Qt then assigns the buffers (ref - vM) * G' / ref — the correction
	/// squared — but persists G', which is what it runs with after a restart;
	/// this port applies the persisted single correction.)</summary>
	void measureScopeGain()
	{
		double vm1, vm2;
		if (!meanVoltageLastSecond(1, &vm1) || !meanVoltageLastSecond(2, &vm2))
		{
			restoreScopeSnapshot();
			fail("Calibration has been abandoned: no oscilloscope data was "
			     "available.  Please check the connection and try again.");
			return;
		}

		// Qt: if((vM > 0.3) | (vM < -0.3)) abandon
		if (vm1 > 0.3 || vm1 < -0.3 || vm2 > 0.3 || vm2 < -0.3)
		{
			restoreScopeSnapshot();
			fail("Calibration has been abandoned due to out-of-range values.  "
			     "Both channels should show approximately 0 V.  Please try "
			     "again.");
			return;
		}

		double ref1 = 3.3 - vref_ch[0];
		double ref2 = 3.3 - vref_ch[1];
		gain_scale_ch[0] = (ref1 - vm1) / ref1;
		gain_scale_ch[1] = (ref2 - vm2) / ref2;
		if (!applyScopeCalToLibrador())
		{
			restoreScopeSnapshot();
			fail("Calibration has been abandoned: librador rejected the "
			     "measured gain values.  Please try again.");
			return;
		}

		scope_valid = true;
		stage = Stage::ScopeDone;
		saveToDevice();
	}

	/// <summary>Qt on_actionCalibrate_2_triggered: workspace to CH1-scope
	/// only, auto-gain off (App-side, see class comment), hardware gain 4,
	/// clear any previous PSU offset so corrections don't stack, then ask the
	/// user to wire CH1 to the PSU output.</summary>
	void startPsuWizard()
	{
		prev_psu_offset = psu_offset;
		prev_psu_valid = psu_valid;
		prev_scope_gain = librador_get_oscilloscope_gain();

		// Qt: driver->psu_offset = 0 ("don't want them to stack!")
		psu_offset = 0.0;
		psu_valid = false;
		librador_set_psu_calibration_offset(0.0);

		if (librador_set_device_mode(kModeCh1Scope) != 0
		    || librador_set_oscilloscope_gain(4) != 0)
		{
			restorePsuSnapshot(false);
			fail("PSU calibration could not start (failed to configure the "
			     "board).  Please try again.");
			return;
		}
		neutraliseChannelView(1);

		stage = Stage::PsuConnectPrompt;
	}

	void restorePsuSnapshot(bool restore_psu_voltage)
	{
		psu_offset = prev_psu_offset;
		psu_valid = prev_psu_valid;
		needs_apply |= (librador_set_psu_calibration_offset(psu_offset) != 0);
		librador_set_oscilloscope_gain(prev_scope_gain);
		if (restore_psu_voltage)
			librador_set_power_supply_voltage(4.5); // Qt: setPsu(4.5) + slider 0
	}

	/// <summary>Qt calibrate_psu_stage2: PSU5 = mean CH1 voltage, valid
	/// (4, 6) V.  Then gain 1, PSU 10 V, settle again.</summary>
	void measurePsu5()
	{
		if (!meanVoltageLastSecond(1, &psu5_measured))
		{
			restorePsuSnapshot(true);
			fail("PSU calibration has been abandoned: no oscilloscope data was "
			     "available.  Please check the connection and try again.");
			return;
		}

		// Qt: if((PSU5 > 6) | (PSU5 < 4)) abandon
		if (psu5_measured > 6.0 || psu5_measured < 4.0)
		{
			restorePsuSnapshot(true);
			fail("PSU calibration has been abandoned due to out-of-range "
			     "values.  The oscilloscope should show approximately 5 V.  "
			     "Please check all wires on your Labrador board and try again.");
			return;
		}

		// Qt: setGain(1); setPsu(10); clearBuffers; singleShot(1800, stage3)
		if (librador_set_oscilloscope_gain(1) != 0
		    || librador_set_power_supply_voltage(10) != 0)
		{
			restorePsuSnapshot(true);
			fail("PSU calibration has been abandoned (failed to configure the "
			     "board for the 10 V step).  Please try again.");
			return;
		}
		settle_deadline = ImGui::GetTime() + kPsuSettleSeconds;
		stage = Stage::Psu10Settle;
	}

	/// <summary>Qt calibrate_psu_stage3: PSU10 = mean CH1 voltage, valid
	/// (8, 12) V; offset = ((PSU5 - 5) + (PSU10 - 10)) / 2, then the PSU is
	/// returned to its resting state.</summary>
	void measurePsu10()
	{
		double psu10_measured;
		bool have_data = meanVoltageLastSecond(1, &psu10_measured);

		// Qt restores the PSU and gain before validating the measurement
		librador_set_power_supply_voltage(4.5);
		librador_set_oscilloscope_gain(prev_scope_gain);

		if (!have_data)
		{
			restorePsuSnapshot(false);
			fail("PSU calibration has been abandoned: no oscilloscope data was "
			     "available.  Please check the connection and try again.");
			return;
		}

		// Qt: if((PSU10 > 12) | (PSU10 < 8)) abandon
		if (psu10_measured > 12.0 || psu10_measured < 8.0)
		{
			restorePsuSnapshot(false);
			fail("PSU calibration has been abandoned due to out-of-range "
			     "values.  The oscilloscope should show approximately 10 V.  "
			     "Please check all wires on your Labrador board and try again.");
			return;
		}

		// Qt: psu_voltage_calibration_offset = ((PSU5 - 5) + (PSU10 - 10)) / 2
		psu_offset = ((psu5_measured - 5.0) + (psu10_measured - 10.0)) / 2.0;
		if (librador_set_psu_calibration_offset(psu_offset) != 0)
		{
			restorePsuSnapshot(false);
			fail("PSU calibration has been abandoned: librador rejected the "
			     "measured offset.  Please try again.");
			return;
		}
		psu_valid = true;
		stage = Stage::PsuDone;
		saveToDevice();
	}

	// ------------------------------------------------------------------
	// Cancel / failure handling
	// ------------------------------------------------------------------

	void cancelWizard()
	{
		switch (stage)
		{
		case Stage::ScopeDisconnectPrompt:
		case Stage::ScopeVrefSettle:
		case Stage::ScopeGroundPrompt:
		case Stage::ScopeGainSettle:
			restoreScopeSnapshot();
			break;
		case Stage::PsuConnectPrompt:
			restorePsuSnapshot(false); // PSU voltage not touched yet
			break;
		case Stage::Psu5Settle:
		case Stage::Psu10Settle:
			restorePsuSnapshot(true);
			break;
		default:
			break;
		}
		stage = Stage::Idle;
	}

	void abortForDisconnect()
	{
		// Restore widget-side state; the librador re-apply is deferred via
		// needs_apply because calibration calls still succeed while
		// disconnected but gain/PSU restores need the link (they are simply
		// skipped — the board reboots to power-on defaults anyway).
		switch (stage)
		{
		case Stage::ScopeDisconnectPrompt:
		case Stage::ScopeVrefSettle:
		case Stage::ScopeGroundPrompt:
		case Stage::ScopeGainSettle:
			restoreScopeSnapshot();
			break;
		case Stage::PsuConnectPrompt:
		case Stage::Psu5Settle:
		case Stage::Psu10Settle:
			psu_offset = prev_psu_offset;
			psu_valid = prev_psu_valid;
			needs_apply = true;
			break;
		default:
			break;
		}
		fail("Calibration has been abandoned: the board was disconnected.");
	}

	void fail(const std::string& message)
	{
		error_text = message;
		stage = Stage::Failed;
	}

	/// <summary>Qt's workspace change also unchecks AC coupling, pause and
	/// the trigger group; reset the librador-side equivalents so measurements
	/// see raw DC-coupled data. (The desktop widgets keep these client-side,
	/// so this is normally a no-op, but Brent-era layouts use them.)</summary>
	static void neutraliseChannelView(int ch)
	{
		librador_set_virtual_transform_settings(
		    ch, o1buffer::virtual_transform_settings{});
		librador_set_trigger_settings(ch, o1buffer::trigger_settings{});
	}

	// ------------------------------------------------------------------
	// UI helpers
	// ------------------------------------------------------------------

	void renderNextCancel()
	{
		ImGui::Dummy(ImVec2(0.0f, 5.0f));
		if (ImGui::Button("Next##cal_next"))
			advance_requested = true;
		ImGui::SameLine();
		if (ImGui::Button("Cancel##cal_cancel"))
			cancel_requested = true;
	}

	void renderMeasuringProgress(double settle_seconds)
	{
		double remaining = settle_deadline - ImGui::GetTime();
		if (remaining < 0.0)
			remaining = 0.0;
		float fraction = 1.0f - (float)(remaining / settle_seconds);
		ImGui::TextWrapped("Measuring, please do not touch the wiring...");
		ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0.0f));
		if (ImGui::Button("Cancel##cal_cancel_measure"))
			cancel_requested = true;
	}

	static std::string formatVolts(double v)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%+.3f V", v);
		return std::string(buf);
	}

	void renderStatusSummary()
	{
		if (scope_valid)
		{
			ImGui::Text("Oscilloscope: calibrated");
			for (int ch : { 1, 2 })
			{
				ImGui::Text("  CH%d  vref %.4f V   gain x%.4f", ch,
				    vref_ch[ch - 1], gain_scale_ch[ch - 1]);
			}
		}
		else
		{
			ImGui::TextColored(constants::GRAY_TEXT,
			    "Oscilloscope: not calibrated (factory defaults)");
		}
		if (psu_valid)
		{
			std::string psu_line
			    = "Power supply: calibrated, offset " + formatVolts(psu_offset);
			ImGui::Text("%s", psu_line.c_str());
		}
		else
		{
			ImGui::TextColored(constants::GRAY_TEXT,
			    "Power supply: not calibrated (factory defaults)");
		}
	}
};
