#include "ControlWidget.hpp"
#include "librador.h"

/// <summary>Oscilloscope Control Widget
/// </summary>
class PlotControl : public ControlWidget
{
public:
	PlotControl(const char* label, ImVec2 size, const float* borderColor)
	    : ControlWidget(label, size, borderColor)
	{
	}

	/// <summary>
	/// Render UI elements for oscilloscope control
	/// </summary>
	void renderControl() override
	{
	}

	/// <summary>
	/// Control sampling settings on labrador board
	/// </summary>
	bool controlLab() override
	{
		return false;
	}

private:
};