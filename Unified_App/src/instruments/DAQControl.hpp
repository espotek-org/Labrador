#pragma once
#include "ControlWidget.hpp"
#include "InputsControl.hpp"
#include "librador.h"
#include "util.h"
#include "platform/file_dialog.h"
#include <string>

/// <summary>DAQ (data logger) widget — records CH A / CH B samples to a text
/// file. Design base: Brent's daq_ui. The device buffer always holds the most
/// recent few seconds of data, so "recording" means dumping the trailing
/// window: librador_daq snapshots the buffer and writes the last
/// (duration x sample rate) samples on its own thread;
/// librador_poll_daq_status() reports when that thread has finished.
/// </summary>
class DAQControl : public ControlWidget
{
public:
	DAQControl(std::string label, ImVec2 size, const float* accentColour)
	    : ControlWidget(label, size, accentColour)
	{}

	/// <summary>
	/// The inputs widget owns the device mode; the DAQ reads it to know which
	/// channels are active, which are logic inputs, and the base sample rate.
	/// App must call this before the first Render().
	/// </summary>
	void SetInputs(InputsControl* inputs_widget)
	{
		inputs = inputs_widget;
	}

	bool isRecording() const { return recording; }

	/// <summary>
	/// Render UI elements for the data logger
	/// </summary>
	void renderControl() override
	{
		IM_ASSERT(inputs != nullptr); // App must call SetInputs() before rendering

		// Finish-detection for the writer thread (Brent's poll_status pattern:
		// poll every frame while active, flash "Saved" once it goes idle)
		if (recording)
		{
			recording = librador_poll_daq_status();
			if (!recording)
				saved_flash = 1.0f; // linger period, mirrors daq_ui's timer2
		}

		ImGui::Dummy(ImVec2(0.0f, 5.0f));

		// The DAQ dumps the device buffer, so it is only meaningful while the
		// iso stream is filling that buffer
		bool stream_active = librador_iso_thread_is_active();
		if (!stream_active)
		{
			ImGui::TextColored(constants::GRAY_TEXT,
			    "Connect a Labrador board to record data.");
		}

		// The units combos must always hold a value that is valid for the
		// channel's current function (daq_ui applies the same reset every
		// frame — librador_daq requires the selected units to be valid)
		for (int ch : { 1, 2 })
		{
			usbCallHandler::daqUnitOptions& u = units_sel[ch - 1];
			if (u == usbCallHandler::daqUnitOptions::None
			    || usbCallHandler::daqUnitIsForScope[u] == chIsLogic(ch))
			{
				u = chIsLogic(ch) ? usbCallHandler::daqUnitOptions::Bits
				                  : usbCallHandler::daqUnitOptions::Volts;
			}
		}

		ImGui::BeginDisabled(!stream_active || recording || dialog_open);

		renderChannelRow(1, "CH1 (OSC1)", &record_a);
		renderChannelRow(2, "CH2 (OSC2)", &record_b);

		// Duration of buffered history to write out (chronological order)
		ImGui::Text("Duration");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ScaledPx(120.0f));
		ImGui::InputFloat("##daq_duration", &duration, 0.0f, 0.0f, "%.3f s");
		duration = ImMin(duration, 10.0f); // device buffer holds ~10 s
		duration = ImMax(duration, 0.0f);
		duration = IM_ROUND(duration * 1000) / 1000.0f;

		// Keep every Nth sample
		ImGui::Text("Downsample");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ScaledPx(120.0f));
		ImGui::InputScalar("##daq_downsample", ImGuiDataType_U8,
		    &downsample_factor, &u8_one, NULL, "%u");
		downsample_factor = ImMax(downsample_factor, u8_one);

		// numToGet / interval_samples, computed exactly as daq_ui does:
		// base ADC rate is 375 kSa/s (750 in the double-rate mode 6), so
		// samples = 1e3 * duration_s * rate_kSa / downsample. Logic channels
		// unpack to 8 bits per ADC sample, but librador applies that x8
		// internally (daq_for_channel passes numToGet * 8 to
		// getMany_singleBit) — numToGet stays in base-rate samples here.
		double base_rate_kSa
		    = (inputs->mode() == InputsControl::Mode::Scope750) ? 750.0 : 375.0;
		int numToGet = (int)(1e3 * (duration * base_rate_kSa) / downsample_factor);
		int interval_samples = downsample_factor;
		ImGui::TextColored(
		    constants::GRAY_TEXT, "%d samples per channel", numToGet);

		bool do_a = record_a && chActive(1);
		bool do_b = record_b && chActive(2);

		ImGui::BeginDisabled(!(do_a || do_b) || duration == 0.0f);
		if (ImGui::Button("Start##daq"))
		{
			// librador_daq channel encoding (see usbcallhandler drive_daq):
			// 1 = CH A, 2 = CH B, 3 = both
			int channel = (do_a ? 1 : 0) | (do_b ? 2 : 0);
			dialog_open = true;
			// Async save dialog; the callback runs on the main thread during
			// a later frame (PumpFileDialogResults), so start the DAQ inside
			// it. TODO(mobile layout): Android needs Brent's documents-dir
			// flow instead of a native save dialog.
			ShowSaveFileDialog("txt",
			    [this, channel, numToGet, interval_samples](const char* path) {
				    dialog_open = false;
				    if (!path)
					    return; // user cancelled
				    // units_sel/out_path are members because the writer
				    // thread reads them after this callback returns
				    out_path = path;
				    if (out_path.size() < 4
				        || out_path.compare(out_path.size() - 4, 4, ".txt") != 0)
				    {
					    out_path += ".txt";
				    }
				    int error = librador_daq(channel, numToGet,
				        interval_samples, units_sel, out_path.c_str());
				    if (error == 1) // librador_daq returns 1 on success
				    {
					    recording = true;
				    }
				    else
				    {
#ifndef NDEBUG
					    // -420/-421: board disconnected while the dialog was open
					    printf("librador_daq FAILED with error code %d\n", error);
#endif
				    }
			    });
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();

		// Status flash (Brent's pattern)
		ImGui::SameLine();
		if (recording)
		{
			ImGui::TextUnformatted("Recording...");
		}
		else if (saved_flash > 0.0f)
		{
			saved_flash -= ImGui::GetIO().DeltaTime;
			ImGui::TextUnformatted("Saved");
		}
	}

	/// <summary>
	/// Nothing to push to the board periodically — recording starts inside
	/// the save-dialog callback and the status poll runs in renderControl().
	/// </summary>
	bool controlLab() override
	{
		return true;
	}

private:
	/// <summary>
	/// One record-enable checkbox + units combo + effective-rate readout
	/// </summary>
	void renderChannelRow(int ch, const char* name, bool* record)
	{
		ImGui::BeginDisabled(!chActive(ch));
		ImGui::Checkbox((std::string(name) + "##daq_rec").c_str(), record);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ScaledPx(90.0f));
		int sel = units_sel[ch - 1];
		if (ImGui::BeginCombo((std::string("##daq_units_") + name).c_str(),
		        usbCallHandler::daq_unit_labels[sel]))
		{
			for (int n = 0; n < usbCallHandler::daqUnitOptions::QUANT; n++)
			{
				// A scope channel records Volts / raw ADC codes; a logic
				// channel records Bits (daq_ui's daqUnitIsForScope filter)
				if (n == usbCallHandler::daqUnitOptions::None
				    || usbCallHandler::daqUnitIsForScope[n] == chIsLogic(ch))
					continue;
				if (ImGui::Selectable(usbCallHandler::daq_unit_labels[n], n == sel))
					units_sel[ch - 1] = (usbCallHandler::daqUnitOptions)n;
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::Text("%.4g kSa/s", effectiveRate_kSa(ch));
		ImGui::EndDisabled();
	}

	/// <summary>
	/// Whether the channel currently produces data (mirrors daq_ui's
	/// ch_enabled: multimeter mode still records CH A)
	/// </summary>
	bool chActive(int ch) const
	{
		if (inputs->mode() == InputsControl::Mode::Multimeter)
			return ch == 1;
		return inputs->channelIsScope(ch) || inputs->channelIsLogic(ch);
	}

	bool chIsLogic(int ch) const
	{
		return inputs->channelIsLogic(ch);
	}

	/// <summary>
	/// Post-downsampling rate written to file, mirrors daq_ui's readout:
	/// base 375 kSa/s (750 in mode 6), x8 for logic channels
	/// </summary>
	float effectiveRate_kSa(int ch) const
	{
		float base = !chActive(ch)
		    ? 0.0f
		    : (inputs->mode() == InputsControl::Mode::Scope750 ? 750.0f : 375.0f);
		float rate = base * (chIsLogic(ch) ? 8.0f : 1.0f);
		return rate / (float)downsample_factor;
	}

	InputsControl* inputs = nullptr;
	// Read by the DAQ writer thread — must stay alive for the whole recording
	usbCallHandler::daqUnitOptions units_sel[2]
	    = { usbCallHandler::daqUnitOptions::Volts,
		  usbCallHandler::daqUnitOptions::Volts };
	std::string out_path;

	bool record_a = true;
	bool record_b = false;
	float duration = 1.0f; // seconds
	const ImU8 u8_one = 1;
	ImU8 downsample_factor = 1;

	bool dialog_open = false;   // save dialog is up, waiting for the callback
	bool recording = false;     // writer thread is running
	float saved_flash = 0.0f;   // seconds left to show "Saved"
};
