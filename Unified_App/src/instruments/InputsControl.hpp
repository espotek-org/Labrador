#pragma once
#include "ControlWidget.hpp"
#include "librador.h"
#include "util.h"

/// <summary>Inputs widget — owns the Labrador device mode (which channel is
/// an oscilloscope or a logic-analyzer input, the 750 kSps single-channel
/// mode, and the multimeter mode). Design base: Brent's inputs_ui. Other
/// widgets (multimeter, logic decode, plot) read the active mode from here.
/// </summary>
class InputsControl : public ControlWidget
{
public:
	// Values are the hardware device modes (see usbcallhandler.cpp isoCallback)
	enum class Mode : int
	{
		Ch1Scope = 0,
		ScopeLogic = 1,
		ScopeScope = 2,
		Ch1Logic = 3,
		LogicLogic = 4,
		Scope750 = 6,
		Multimeter = 7,
	};

	InputsControl(std::string label, ImVec2 size, const float* accentColour)
	    : ControlWidget(label, size, accentColour)
	{}

	Mode mode() const { return selected_mode; }
	bool modeChangedThisFrame() const { return mode_changed; }

	// Mode list + programmatic selection, for layouts that render their own
	// mode selector (desktop toolbar / Device menu) instead of this widget.
	static constexpr int ModeCount = 7;
	static const char* modeLabel(int idx) { return mode_labels[idx]; }
	int selectedIndex() const { return selected_idx; }
	void selectIndex(int idx)
	{
		selected_idx = idx;
		selected_mode = mode_values[idx];
	}
	void setMode(Mode m)
	{
		for (int i = 0; i < ModeCount; i++)
		{
			if (mode_values[i] == m)
			{
				selectIndex(i);
				return;
			}
		}
	}

	bool channelIsScope(int ch) const
	{
		switch (selected_mode)
		{
		case Mode::Ch1Scope: return ch == 1;
		case Mode::ScopeLogic: return ch == 1;
		case Mode::ScopeScope: return true;
		case Mode::Scope750: return ch == 1;
		default: return false;
		}
	}

	bool channelIsLogic(int ch) const
	{
		switch (selected_mode)
		{
		case Mode::ScopeLogic: return ch == 2;
		case Mode::Ch1Logic: return ch == 1;
		case Mode::LogicLogic: return true;
		default: return false;
		}
	}

	// Force the mode to be resent on the next controlLab (used after reconnect)
	void markDirty() { applied_mode = -1; }

	void renderControl() override
	{
		ImGui::Dummy(ImVec2(0.0f, 5.0f));
		ImGui::Text("Mode");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##inputs_mode", mode_labels[selected_idx]))
		{
			for (int i = 0; i < (int)IM_ARRAYSIZE(mode_values); i++)
			{
				bool is_selected = (i == selected_idx);
				if (ImGui::Selectable(mode_labels[i], is_selected))
				{
					selected_idx = i;
					selected_mode = mode_values[i];
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}

	bool controlLab() override
	{
		mode_changed = false;
		if ((int)selected_mode == applied_mode)
			return true;
		int error = librador_set_device_mode((int)selected_mode);
		if (error)
		{
#ifndef NDEBUG
			printf("librador_set_device_mode FAILED with error code %d\n", error);
#endif
			return false;
		}
		applied_mode = (int)selected_mode;
		mode_changed = true;
		return true;
	}

private:
	static constexpr Mode mode_values[7] = { Mode::ScopeScope, Mode::Ch1Scope,
		Mode::Scope750, Mode::ScopeLogic, Mode::Ch1Logic, Mode::LogicLogic,
		Mode::Multimeter };
	static constexpr const char* mode_labels[7] = {
		"CH1 + CH2 oscilloscope",
		"CH1 oscilloscope",
		"CH1 oscilloscope, 750 kSps",
		"CH1 oscilloscope + CH2 logic",
		"CH1 logic",
		"CH1 + CH2 logic",
		"Multimeter (CH1)",
	};

	Mode selected_mode = Mode::ScopeScope;
	int selected_idx = 0;
	int applied_mode = -1;
	bool mode_changed = false;
};
