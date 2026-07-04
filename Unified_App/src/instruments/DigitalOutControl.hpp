#pragma once
#include "ControlWidget.hpp"
#include "librador.h"
#include "util.h"

/// <summary>Digital output widget — four on/off toggles driving the
/// Labrador's DO1..DO4 pins (device command a6). librador keeps the packed
/// 4-bit output state; this widget just tells it which channel changed.
/// </summary>
class DigitalOutControl : public ControlWidget
{
public:
	DigitalOutControl(std::string label, ImVec2 size, const float* accentColour)
	    : ControlWidget(label, size, accentColour)
	{}

	// Force every channel to be resent on the next controlLab (used after
	// reconnect, like InputsControl::markDirty)
	void markDirty()
	{
		for (int i = 0; i < 4; i++)
			applied_state[i] = -1;
	}

	/// <summary>
	/// Render UI elements for the digital outputs
	/// </summary>
	void renderControl() override
	{
		ImGui::Dummy(ImVec2(0.0f, 5.0f));
		for (int i = 0; i < 4; i++)
		{
			if (i > 0)
				ImGui::SameLine();
			ImGui::Checkbox(do_labels[i], &state[i]);
		}
	}

	/// <summary>
	/// Send the digital output states to the labrador board.
	/// Only channels that changed since the last successful send go out.
	/// </summary>
	bool controlLab() override
	{
		bool ok = true;
		for (int i = 0; i < 4; i++)
		{
			if ((int)state[i] == applied_state[i])
				continue;
			int error = librador_set_digital_out(i + 1, state[i]);
			if (error)
			{
#ifndef NDEBUG
				printf("librador_set_digital_out(%d) FAILED with error code "
				       "%d\n",
				    i + 1, error);
#endif
				// Board not connected (-420/-421) or the transfer failed —
				// recoverable either way, keep running and retry next pass
				ok = false;
				continue;
			}
			applied_state[i] = state[i] ? 1 : 0;
		}
		return ok;
	}

private:
	static constexpr const char* do_labels[4] = { "DO1", "DO2", "DO3", "DO4" };
	bool state[4] = { false, false, false, false };
	// Last state acked by the board per channel; -1 = unknown, resend.
	// Outputs power up low, so a fresh widget already matches the hardware.
	int applied_state[4] = { 0, 0, 0, 0 };
};
