#include "ControlWidget.hpp"
#include "librador.h"
#include "util.h"

/// <summary>Power Suppy Unit Widget
/// </summary>
class PSUControl : public ControlWidget
{
public:
	float voltage;

	PSUControl(std::string label, ImVec2 size, const float* borderColor)
	    : ControlWidget(label, size, borderColor)
	    , voltage(4.5f) // Default voltage: the supply's minimum (safe default)
	{}

	/// <summary>
	/// Render UI elements for power supply unit
	/// </summary>
	void renderControl() override
	{
		ImGui::Dummy(ImVec2(0.0f, 5.0f));
		ImGui::Text("Voltage");
		ImGui::SameLine();
		ImGui::SliderFloat("##voltage", &voltage, 4.5f, 11.0f, "%.1f V");
	}

	/// <summary>
	/// Set the Power Supply Voltage on the labrador board.
	/// </summary>
	bool controlLab() override
	{
		int error = librador_set_power_supply_voltage(voltage);
		if (error)
		{
			// The set failed — almost always because the board isn't
			// connected (librador returns -420/-421 for API/USB-not-ready,
			// -1101/-1104 for a failed control transfer). All of these are
			// transient and expected: the board may be mid-reset (Esc), or
			// unplugged. Report "not applied" so the per-frame controlLab
			// pass retries once the link is back — never terminate the app.
			// (This used to std::exit(error): a USB reset returned -421 and
			// killed the whole GUI, exit code 91.)
#ifndef NDEBUG
			printf("librador_set_power_supply_voltage FAILED with error code "
				    "%d; will retry\n",
				error);
#endif
			return false;
		}
		return true;
	}
};
