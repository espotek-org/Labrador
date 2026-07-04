#include "util.h"
#include <iostream>
#include <sstream>
#include <iomanip>

float constants::x_preview[constants::PREVIEW_RES+1];
float constants::sine_preview[constants::PREVIEW_RES+1];
float constants::square_preview[constants::PREVIEW_RES+1];
float constants::sawtooth_preview[constants::PREVIEW_RES+1];
float constants::triangle_preview[constants::PREVIEW_RES+1];



/// <summary>
/// Initialise global preview arrays for Signal Generator control
/// </summary>
void init_constants()
{
	const int pr = constants::PREVIEW_RES;
	for (int i = 0; i < pr; i++)
	{
		constants::x_preview[i] = i * 1.0f;
		constants::sine_preview[i] = sinf(i * (1.0f / pr) * 2 * M_PI);
		constants::sawtooth_preview[i] = -1.0f + 2.0f / pr * i;
		if (i < pr / 2)
		{
			constants::square_preview[i] = -1.0f;
			constants::triangle_preview[i] = -1.0f + 4.0f / pr * i;
		}
		else
		{
			constants::square_preview[i] = 1.0f;
			constants::triangle_preview[i] = constants::triangle_preview[pr - i - 1];
		}
	}

	// Wraparound for continuous plot
	constants::x_preview[pr] = pr * 1.0f;
	constants::sine_preview[pr] = constants::sine_preview[0];
	constants::square_preview[pr] = constants::square_preview[0];
	constants::triangle_preview[pr] = constants::triangle_preview[0];
	constants::sawtooth_preview[pr] = constants::sawtooth_preview[0];
}

/// <summary>
/// Replaces all instances of a substring with a provided string
/// </summary>
/// <param name="s"></param>
/// <param name="toReplace"></param>
/// <param name="replaceWith"></param>
void replace_all(std::string& s, std::string const& toReplace, std::string const& replaceWith)
{
	std::string buf;
	std::size_t pos = 0;
	std::size_t prevPos;

	// Reserves rough estimate of final size of string.
	buf.reserve(s.size());

	while (true)
	{
		prevPos = pos;
		pos = s.find(toReplace, pos);
		if (pos == std::string::npos)
			break;
		buf.append(s, prevPos, pos - prevPos);
		buf += replaceWith;
		pos += toReplace.size();
	}

	buf.append(s, prevPos, s.size() - prevPos);
	s.swap(buf);
}

/*
STYLES
*/

// Current theme, recorded by SetGlobalStyle so mid-frame style helpers
// (PreviewStyle) pick colours that stay readable in both themes.
static bool s_dark_theme = true;

/// <summary>
/// Global theme, reapplied every frame from App::Update (so it also wins
/// over the dark colours AppBase sets at init). dark = the original Monash
/// look; light = ImGui/ImPlot light palettes with matching overrides.
/// </summary>
void SetGlobalStyle(bool dark)
{
	s_dark_theme = dark;
	ImVec4* colors = ImGui::GetStyle().Colors;
	if (dark)
	{
		// Full palette reset first, then the Monash overrides — covers every
		// colour a previous light frame (or AppBase's init) may have touched.
		ImGui::StyleColorsDark();
		ImPlot::StyleColorsDark();
		colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.16f, 0.62f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.16f, 0.16f, 1.0f);
		colors[ImGuiCol_CheckMark] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		colors[ImGuiCol_SliderGrab] = ImVec4(1.00f, 1.00f, 1.00f, 0.20f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.78f);
		colors[ImGuiCol_Button] = ImVec4(1.00f, 1.00f, 1.00f, 0.20f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.39f);
		colors[ImGuiCol_ButtonActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.59f);
		colors[ImGuiCol_Header] = ImVec4(0.26f, 0.59f, 0.98f, 0.00f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.00f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.00f);
		colors[ImGuiCol_Tab] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
		colors[ImGuiCol_TabActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
	}
	else
	{
		// Light counterpart of every dark override above: the dark theme
		// brightens with translucent white overlays, so the light theme
		// darkens with translucent black ones — the widget accent colours
		// (BeginControlWidgetStyle) stay readable over both.
		ImGui::StyleColorsLight();
		ImPlot::StyleColorsLight();
		colors[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.98f, 0.98f, 0.98f, 1.00f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.78f, 0.78f, 0.78f, 0.62f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.62f, 0.62f, 0.62f, 1.0f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.78f, 0.78f, 0.78f, 1.0f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.00f, 0.00f, 0.00f, 0.20f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.00f, 0.00f, 0.00f, 0.78f);
		colors[ImGuiCol_Button] = ImVec4(0.00f, 0.00f, 0.00f, 0.14f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.00f, 0.00f, 0.00f, 0.28f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 0.00f, 0.00f, 0.42f);
		colors[ImGuiCol_Header] = ImVec4(0.26f, 0.59f, 0.98f, 0.00f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.00f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.00f);
		colors[ImGuiCol_Tab] = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
		colors[ImGuiCol_TabActive] = ImVec4(0.72f, 0.72f, 0.72f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.66f, 0.66f, 0.66f, 1.00f);
	}

	// Plot chrome shared by both themes. The plot background stays
	// transparent so plots inherit WindowBg (as the Monash app did); the grid
	// and inlay text flip contrast with the theme. PreviewStyle() mutates
	// these same globals mid-frame, so it uses identical values.
	ImVec4* plot_colors = ImPlot::GetStyle().Colors;
	plot_colors[ImPlotCol_FrameBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	plot_colors[ImPlotCol_PlotBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	plot_colors[ImPlotCol_PlotBorder] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	plot_colors[ImPlotCol_AxisGrid] = dark ? ImVec4(1.0f, 1.0f, 1.0f, 0.9f)
	                                       : ImVec4(0.15f, 0.15f, 0.15f, 0.9f);
	plot_colors[ImPlotCol_InlayText] = dark ? ImVec4(1.0f, 1.0f, 1.0f, 0.5f)
	                                        : ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
}

/// <summary>
/// Defines style for the Signal Generator Preview plot
/// </summary>
void PreviewStyle()
{
	ImPlotStyle& style = ImPlot::GetStyle();

	ImVec4* colors = style.Colors;
	// colors[ImPlotCol_Line] = ImVec4(1.0f, 1.0f, 1.0f, 0.5f); // WHITE
	// colors[ImPlotCol_Fill] = ImVec4(1.0f, 1.0f, 1.0f, 0.0f); // TRANSPARENT
	//colors[ImPlotCol_MarkerOutline] = IMPLOT_AUTO_COL;
	//colors[ImPlotCol_MarkerFill] = IMPLOT_AUTO_COL;
	//colors[ImPlotCol_ErrorBar] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
	colors[ImPlotCol_FrameBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
	colors[ImPlotCol_PlotBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.0f); // TRANSPARENT
	colors[ImPlotCol_PlotBorder] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	//colors[ImPlotCol_LegendBg] = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
	//colors[ImPlotCol_LegendBorder] = ImVec4(0.80f, 0.81f, 0.85f, 1.00f);
	//colors[ImPlotCol_LegendText] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
	//colors[ImPlotCol_TitleText] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
	// Grid/inlay contrast flips with the theme; values match SetGlobalStyle
	// (these globals leak past the preview, so keep the two in agreement).
	colors[ImPlotCol_InlayText] = s_dark_theme ? ImVec4(1.0f, 1.0f, 1.0f, 0.5f)
	                                           : ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
	//colors[ImPlotCol_AxisText] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
	colors[ImPlotCol_AxisGrid] = s_dark_theme ? ImVec4(1.0f, 1.0f, 1.0f, 0.9f)
	                                          : ImVec4(0.15f, 0.15f, 0.15f, 0.9f);
	//colors[ImPlotCol_AxisBgHovered] = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
	//colors[ImPlotCol_AxisBgActive] = ImVec4(0.92f, 0.92f, 0.95f, 0.75f);
	//colors[ImPlotCol_Selection] = ImVec4(1.00f, 0.65f, 0.00f, 1.00f);
	//colors[ImPlotCol_Crosshairs] = ImVec4(0.23f, 0.10f, 0.64f, 0.50f);

	// ImPlot fork: item styling moved from ImPlotStyle to per-call ImPlotSpec;
	// line weight/marker size are set at the PlotLine call sites instead.
	// style.LineWeight = 2.5;
	// style.Marker = ImPlotMarker_Plus;
	// style.MarkerSize = 2;
	//style.MarkerWeight = 1;
	//style.FillAlpha = 1.0f;
	//style.ErrorBarSize = 5;
	//style.ErrorBarWeight = 1.5f;
	//style.DigitalBitHeight = 8;
	//style.DigitalBitGap = 4;
	//style.PlotBorderSize = 0;
	//style.MinorAlpha = 1.0f;
	//style.MajorTickLen = ImVec2(0, 0);
	//style.MinorTickLen = ImVec2(0, 0);
	//style.MajorTickSize = ImVec2(0, 0);
	//style.MinorTickSize = ImVec2(0, 0);
	//style.MajorGridSize = ImVec2(1.2f, 1.2f);
	//style.MinorGridSize = ImVec2(1.2f, 1.2f);
	style.PlotPadding = ImVec2(0, 0);
	//style.LabelPadding = ImVec2(5, 5);
	//style.LegendPadding = ImVec2(5, 5);
	//style.MousePosPadding = ImVec2(5, 5);
	//style.PlotMinSize = ImVec2(300, 225);
}

/// <summary>
/// Defines the style of the general control widget
/// </summary>
/// <param name="ac">Accent colour (RGB 0..1) </param>
void BeginControlWidgetStyle(const float ac[3]) {
	const ImVec4 accent(ac[0], ac[1], ac[2], 1.0f);

	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(accent.x, accent.y, accent.z, 0.50f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(accent.x, accent.y, accent.z, 0.65f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(accent.x, accent.y, accent.z, 1.00f));
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(accent.x, accent.y, accent.z, 0.50f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(accent.x, accent.y, accent.z, 0.65f));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(accent.x, accent.y, accent.z, 1.00f));
	// ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(accent.x * 0.75f, accent.y * 0.75f, accent.z * 0.75f, 1.00f));
	// ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(std::min(1.f, accent.x * 1.05f), std::min(1.f, accent.y * 1.05f), std::min(1.f, accent.z * 1.05f), 1.00f));
	// ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(accent.x, accent.y, accent.z, 1.00f));
	// ImGui::PushStyleColor(ImGuiCol_TabUnfocused, ImVec4(accent.x * 0.60f, accent.y * 0.60f, accent.z * 0.60f, 1.00f));
	// ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, ImVec4(accent.x * 0.70f, accent.y * 0.70f, accent.z * 0.70f, 1.00f));
}

void EndControlWidgetStyle() {
	ImGui::PopStyleColor(7);
}

ImU32 colourConvert(const float c[3], float alpha)
{
	return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], alpha));
}

void MultiplyButtonColour(ImU32* ButtonColour, float multiplier)
{
	ImColor im_ButtonColour = ImColor(*ButtonColour);

	im_ButtonColour.Value.x = multiplier * im_ButtonColour.Value.x > 1 ? 1 : multiplier * im_ButtonColour.Value.x;
	im_ButtonColour.Value.y = multiplier * im_ButtonColour.Value.y > 1 ? 1 : multiplier * im_ButtonColour.Value.y;
	im_ButtonColour.Value.z = multiplier * im_ButtonColour.Value.z > 1 ? 1 : multiplier * im_ButtonColour.Value.z;


	*ButtonColour = ImU32(im_ButtonColour);
}

std::string NumToString(double num, int precision)
{
	std::stringstream stream;
	stream << std::fixed << std::setprecision(precision) << num;
	std::string str = stream.str();
	return str;
}
int MetricFormatter(double value, char* buff, int size, void* data) {
	const char* unit = (const char*)data;
	static double v[] = { 1000000000,1000000,1000,1,0.001,0.000001,0.000000001 };
	static const char* p[] = { "G","M","k","","m",u8"\u03bc","n" };
	if (value == 0) {
		return snprintf(buff, size, "0 %s", unit);
	}
	for (int i = 0; i < 7; ++i) {
		if (fabs(value) >= v[i]) {
			return snprintf(buff, size, "%g %s%s", value / v[i], p[i], unit);
		}
	}
	return snprintf(buff, size, "%g %s%s", value / v[6], p[6], unit);
}
void ToggleTriggerTypeComboChannel(int* ComboCurrentItem)
{
	switch (*ComboCurrentItem)
	{
		case 0:
			*ComboCurrentItem = 2;
			break;
		case 1:
			*ComboCurrentItem = 3;
			break;
		case 2:
			*ComboCurrentItem = 0;
			break;
		case 3:
			*ComboCurrentItem = 1;
	}
}
void ToggleTriggerTypeComboType(int* ComboCurrentItem)
{
	switch (*ComboCurrentItem)
	{
	case 0:
		*ComboCurrentItem = 1;
		break;
	case 1:
		*ComboCurrentItem = 0;
		break;
	case 2:
		*ComboCurrentItem = 3;
		break;
	case 3:
		*ComboCurrentItem = 2;
	}
}
std::vector<double> EvalUserExpression(std::string user_text, std::vector<double> osc1, std::vector<double> osc2, std::vector<double> time, ParseStatus& parse_status)
{
	parse_status.success = false;

	// --------------------------------------------------------------------
	// If either osc is empty, replace it with a zero vector sized to "time"
	// --------------------------------------------------------------------
	std::size_t T = time.size();

	if (T == 0) {
		// If there is literally no time vector, nothing can be evaluated.
		parse_status.success = false;
		return {};
	}

	if (osc1.empty())
		osc1.assign(T, 0.0);

	if (osc2.empty())
		osc2.assign(T, 0.0);

	// Output vector size always matches time
	std::vector<double> result(T, 0.0);

	// --------------------------------------------------------------------
	// Build symbol table and expression
	// --------------------------------------------------------------------
	exprtk::symbol_table<double> sym;
	sym.add_vector("osc1", osc1);
	sym.add_vector("osc2", osc2);
	sym.add_vector("t", time);
	sym.add_vector("result", result);
	sym.add_constants();

	std::string src = "result := (" + user_text + ");";

	exprtk::expression<double> expr;
	expr.register_symbol_table(sym);
	exprtk::parser<double> parser;

	// --------------------------------------------------------------------
	// Compile
	// --------------------------------------------------------------------
	if (!parser.compile(src, expr))
	{
		parse_status.success = false;
		return result;
	}

	// --------------------------------------------------------------------
	// Evaluate
	// --------------------------------------------------------------------
	expr.value(); // fills "result"

	parse_status.success = true;
	return result;
}

// Drop-in helper: shows a 0..1 value as "0%..100%" and writes back scaled.
// fmt e.g. "%.0f%%" or "%.1f%%"
bool SliderFloatPercent(const char* label, float* v01,
	const char* fmt,
	ImGuiSliderFlags flags)
{
	float pct = (*v01) * 100.0f;
	bool changed = ImGui::SliderFloat(label, &pct, 0.0f, 100.0f, fmt, flags);
	if (changed) *v01 = pct / 100.0f;
	return changed;
}

std::vector<float> linspace(float x_min, float x_max, int resolution) {
    std::vector<float> result(resolution);
    float step = (x_max - x_min) / (resolution - 1);
    
    for (int i = 0; i < resolution; i++) {
        result[i] = x_min + i * step;
    }
    
    return result;
}


ImVec4 GetPlotColour(PlotColour c) {
	switch (c) {
	case Plot_Red:     return ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
	case Plot_Green:   return ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
	case Plot_Blue:    return ImVec4(0.0f, 0.5f, 1.0f, 1.0f);
	case Plot_Yellow:  return ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
	case Plot_Cyan:    return ImVec4(0.0f, 1.0f, 1.0f, 1.0f);
	case Plot_Magenta: return ImVec4(1.0f, 0.0f, 1.0f, 1.0f);
	default:           return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

bool CheckIfInSafetyMode()
{
	int channel = 1;
	double time_window = 0.5;
	double sample_rate_hz = 30;
	double delay = 0;
	int filter_mode = 0;
	double safety_mode_data_value = -10.174999999999999;
	std::vector<double>* data = librador_get_analog_data_by_rate(channel, time_window, sample_rate_hz, delay, filter_mode);
	if (data != nullptr)
	{
		for (int i = 0; i < data->size(); i++)
		{
			if ((*data)[i] != safety_mode_data_value)
			{
				return false;
			}
		}
	}
	else
	{
		return false;
	}
	return true;
}
bool CheckIfInUninitialisedMode()
{
	int channel = 1;
	double time_window = 0.5;
	double sample_rate_hz = 30;
	double delay = 0;
	int filter_mode = 0;

	std::vector<double>* data =
		librador_get_analog_data_by_rate(channel, time_window, sample_rate_hz, delay, filter_mode);

	if (data == nullptr || data->empty())
		return false;

	// Take the first sample as reference
	double first = (*data)[0];

	for (double v : *data)
	{
		if (v != first)
			return false;   // not constant
	}

	return true; // all values are identical

}
// Export Stuff
std::string BuildDelimited2Col(const std::vector<double>& x,
	const std::vector<double>& y,
	const char* xHeader,
	const char* yHeader,
	char sep)
{
	std::string s;
	s.reserve(64 + x.size() * 32);

	s += xHeader ? xHeader : "X";
	s += sep;
	s += yHeader ? yHeader : "Y";
	s += '\n';

	const size_t n = std::min(x.size(), y.size());
	for (size_t i = 0; i < n; ++i) {
		s += std::to_string(x[i]);
		s += sep;
		s += std::to_string(y[i]);
		s += '\n';
	}
	return s;
}

bool Export2ColToClipboard(const std::vector<double>& x,
	const std::vector<double>& y,
	const char* xHeader,
	const char* yHeader)
{
	// NOTE: allow empty data; BuildDelimited2Col will then emit header-only.
	std::string clipboardStr = BuildDelimited2Col(x, y, xHeader, yHeader, '\t');
	ImGui::SetClipboardText(clipboardStr.c_str());
	return true;
}

bool Export2ColToCsvFile(const char* basePath,
	const char* fileExtension,
	const std::vector<double>& x,
	const std::vector<double>& y,
	const char* xHeader,
	const char* yHeader)
{
	if (!basePath || !*basePath)
		return false; // still need a valid path

	// Allow empty x/y: header-only CSV is fine.
	std::string path(basePath);

	std::string ext = ".";
	ext += (fileExtension && *fileExtension) ? fileExtension : "csv";

	if (path.size() < ext.size() ||
		path.compare(path.size() - ext.size(), ext.size(), ext) != 0)
	{
		path += ext;
	}

	std::ofstream file(path);
	if (!file.is_open())
		return false;

	std::string fileStr = BuildDelimited2Col(x, y, xHeader, yHeader, ',');
	file << fileStr;
	return true;
}




