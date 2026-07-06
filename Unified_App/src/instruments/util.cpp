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

// Retinted per theme by SetGlobalStyle (declared extern in util.h).
ImVec4 constants::GRAY_TEXT = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

// Mutable per-instrument accents (theme-written; classic values as defaults).
// Widgets hold live pointers to these arrays, so rewriting the contents
// re-skins every accent user — boxed titles, switches, wave previews.
float constants::PSU_ACCENT[3] = { 190.f / 255, 54.f / 255, 54.f / 255 };
float constants::SG1_ACCENT[3] = { 42.f / 255, 39.f / 255, 212.f / 255 };
float constants::SG2_ACCENT[3] = { 203.f / 255, 100.f / 255, 4.f / 255 };
float constants::OSC_ACCENT[3] = { 0.4f, 0.4f, 0.4f };
float constants::GEN_ACCENT[3] = { 150.f / 255, 150.f / 255, 150.f / 255 };
float constants::SPECTRUM_ANALYSER_ACCENT[3] = { 210.f / 255, 68.f / 255, 41.f / 255 };
float constants::NETWORK_ANALYSER_ACCENT[3] = { 65.f / 255, 194.f / 255, 55.f / 255 };
float constants::OSC1_ACCENT[3] = { 1.0f, 1.0f, 0.0f };
float constants::OSC2_ACCENT[3] = { 0.0f, 1.0f, 1.0f };
float constants::MATH_ACCENT[3] = { 0.0f, 0.9f, 0.78f };

// The standard bright trace colours (every theme except classic light).
#define BRIGHT_TRACES { 1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 1.0f }, { 0.0f, 0.9f, 0.78f }

// The original Monash chrome palette (used by the classic dark theme).
#define CLASSIC_ACCENTS                                                          \
	{                                                                            \
		{ 190.f / 255, 54.f / 255, 54.f / 255 },   /* psu */                     \
		{ 42.f / 255, 39.f / 255, 212.f / 255 },   /* sg1 */                     \
		{ 203.f / 255, 100.f / 255, 4.f / 255 },   /* sg2 */                     \
		{ 150.f / 255, 150.f / 255, 150.f / 255 }, /* gen */                     \
		{ 210.f / 255, 68.f / 255, 41.f / 255 },   /* logic */                   \
		{ 65.f / 255, 194.f / 255, 55.f / 255 },   /* record */                  \
		{ 0.4f, 0.4f, 0.4f },                      /* scope */                   \
		BRIGHT_TRACES,                                                           \
	}

// Single-phosphor palette: every instrument accent is the line colour; the
// trace colours stay data-true so channels remain identifiable.
#define MONO_ACCENTS(r, g, b)                                                    \
	{                                                                            \
		{ r, g, b }, { r, g, b }, { r, g, b }, { r, g, b }, { r, g, b },         \
		    { r, g, b }, { r, g, b }, BRIGHT_TRACES,                             \
	}

// ---- Theme table -------------------------------------------------------------
// The CRT family is one design in three phosphors — P1 green ("Phosphor"),
// P3 amber ("Amber") and a white XY/vector monitor ("Vector") — each in two
// cuts: "Retro" (VT323 terminal font) and "Modern" (Rajdhani). The classic
// themes are the original Monash looks.

// Colour blocks shared by the Retro/Modern cut of each CRT scheme.
#define PHOSPHOR_COLOURS                                                         \
	/*bg*/ ImVec4(0.010f, 0.024f, 0.012f, 1.0f),                                 \
	    /*text*/ ImVec4(0.62f, 1.00f, 0.66f, 1.0f),                              \
	    /*dim*/ ImVec4(0.33f, 0.60f, 0.38f, 1.0f),                               \
	    /*line*/ ImVec4(0.15f, 0.88f, 0.34f, 1.0f),                              \
	    /*lineDim*/ ImVec4(0.09f, 0.42f, 0.17f, 1.0f),                           \
	    /*fill*/ ImVec4(0.05f, 0.17f, 0.08f, 0.80f),                             \
	    /*screen*/ ImVec4(0.004f, 0.030f, 0.010f, 1.0f),                         \
	    /*grid*/ ImVec4(0.16f, 0.80f, 0.32f, 0.30f),                             \
	    MONO_ACCENTS(0.15f, 0.88f, 0.34f)

#define AMBER_COLOURS                                                            \
	ImVec4(0.030f, 0.020f, 0.006f, 1.0f), ImVec4(1.00f, 0.78f, 0.28f, 1.0f),     \
	    ImVec4(0.62f, 0.44f, 0.15f, 1.0f), ImVec4(1.00f, 0.69f, 0.10f, 1.0f),    \
	    ImVec4(0.48f, 0.32f, 0.05f, 1.0f), ImVec4(0.18f, 0.11f, 0.02f, 0.80f),   \
	    ImVec4(0.028f, 0.018f, 0.004f, 1.0f),                                    \
	    ImVec4(0.95f, 0.65f, 0.10f, 0.26f), MONO_ACCENTS(1.00f, 0.69f, 0.10f)

#define VECTOR_COLOURS                                                           \
	ImVec4(0.012f, 0.012f, 0.016f, 1.0f), ImVec4(0.93f, 0.96f, 1.00f, 1.0f),     \
	    ImVec4(0.55f, 0.58f, 0.64f, 1.0f), ImVec4(0.88f, 0.92f, 1.00f, 1.0f),    \
	    ImVec4(0.36f, 0.39f, 0.46f, 1.0f), ImVec4(0.13f, 0.14f, 0.17f, 0.80f),   \
	    ImVec4(0.005f, 0.005f, 0.009f, 1.0f),                                    \
	    ImVec4(0.82f, 0.86f, 1.00f, 0.20f), MONO_ACCENTS(0.88f, 0.92f, 1.00f)

static const ThemeSpec THEMES[] = {
	// Classic looks (the original Monash palettes) — Classic Dark is the
	// default. The chrome fields are only read by theme-aware helpers, so
	// subtle grays keep them non-committal.
	{ "classic-dark", "Classic Dark", false, false, false, false,
	    ImVec4(0.06f, 0.06f, 0.06f, 1.0f), ImVec4(1, 1, 1, 1),
	    ImVec4(0.6f, 0.6f, 0.6f, 1.0f), ImVec4(1, 1, 1, 0.5f),
	    ImVec4(0.5f, 0.5f, 0.5f, 0.5f), ImVec4(0.16f, 0.16f, 0.16f, 0.62f),
	    ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 0.9f), CLASSIC_ACCENTS },
	{ "classic-light", "Classic Light", false, false, true, false,
	    ImVec4(0.94f, 0.94f, 0.94f, 1.0f), ImVec4(0, 0, 0, 1),
	    ImVec4(0.35f, 0.35f, 0.35f, 1.0f), ImVec4(0, 0, 0, 0.5f),
	    ImVec4(0.4f, 0.4f, 0.4f, 0.5f), ImVec4(0.78f, 0.78f, 0.78f, 0.62f),
	    ImVec4(0, 0, 0, 0), ImVec4(0.15f, 0.15f, 0.15f, 0.9f),
	    // Light-mode palette: the dark-theme chrome mostly holds up on white,
	    // but bright yellow/cyan don't — darken the traces (same hue family)
	    // and the near-white generic accent so everything stays legible.
	    { { 0.72f, 0.16f, 0.16f },   /* psu */
	      { 0.16f, 0.15f, 0.75f },   /* sg1 */
	      { 0.72f, 0.36f, 0.02f },   /* sg2 */
	      { 0.42f, 0.42f, 0.42f },   /* gen: darker so ON reads on light gray */
	      { 0.75f, 0.22f, 0.13f },   /* logic */
	      { 0.14f, 0.55f, 0.12f },   /* record */
	      { 0.35f, 0.35f, 0.35f },   /* scope */
	      { 0.70f, 0.52f, 0.00f },   /* ch1: dark gold  */
	      { 0.00f, 0.48f, 0.55f },   /* ch2: dark teal  */
	      { 0.00f, 0.52f, 0.42f } } }, /* math: dark sea-green */
	// CRT family — P1 green phosphor
	{ "phosphor-retro", "Phosphor Retro", true, true, false, true, PHOSPHOR_COLOURS },
	{ "phosphor-modern", "Phosphor Modern", true, true, false, false, PHOSPHOR_COLOURS },
	// P3 amber phosphor
	{ "amber-retro", "Amber Retro", true, true, false, true, AMBER_COLOURS },
	{ "amber-modern", "Amber Modern", true, true, false, false, AMBER_COLOURS },
	// White XY/vector monitor
	{ "vector-retro", "Vector Retro", true, true, false, true, VECTOR_COLOURS },
	{ "vector-modern", "Vector Modern", true, true, false, false, VECTOR_COLOURS },
};

static const ThemeSpec* s_theme = &THEMES[0];

int ThemeCount() { return IM_ARRAYSIZE(THEMES); }
const ThemeSpec& ThemeAt(int idx) { return THEMES[idx]; }
const ThemeSpec& CurrentTheme() { return *s_theme; }

const ThemeSpec* FindTheme(const std::string& id)
{
	for (const ThemeSpec& t : THEMES)
		if (id == t.id)
			return &t;
	return nullptr;
}

/// <summary>
/// Global theme, reapplied every frame from App::Update (so it also wins
/// over the dark colours AppBase sets at init and any mid-frame mutation).
/// </summary>
void SetGlobalStyle(const ThemeSpec& t)
{
	s_theme = &t;
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;
	// AppBase stores the UI scale here; rounding is set per frame because the
	// themes disagree about it (retro = square corners), so scale by hand.
	const float sc = style.FontScaleDpi > 0.0f ? style.FontScaleDpi : 1.0f;
	auto A = [](ImVec4 c, float a) { c.w = a; return c; };

	// Hint text follows the theme (widgets reference constants::GRAY_TEXT).
	constants::GRAY_TEXT = t.retro ? t.dim
	    : t.light                  ? ImVec4(0.40f, 0.40f, 0.40f, 1.0f)
	                               : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

	// Install the theme's per-instrument accent palette. The widgets hold
	// live pointers into these arrays, so this re-skins every accent user.
	auto copy3 = [](float dst[3], const float src[3]) {
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = src[2];
	};
	copy3(constants::PSU_ACCENT, t.accents.psu);
	copy3(constants::SG1_ACCENT, t.accents.sg1);
	copy3(constants::SG2_ACCENT, t.accents.sg2);
	copy3(constants::GEN_ACCENT, t.accents.gen);
	copy3(constants::SPECTRUM_ANALYSER_ACCENT, t.accents.logic);
	copy3(constants::NETWORK_ANALYSER_ACCENT, t.accents.record);
	copy3(constants::OSC_ACCENT, t.accents.scope);
	copy3(constants::OSC1_ACCENT, t.accents.ch1);
	copy3(constants::OSC2_ACCENT, t.accents.ch2);
	copy3(constants::MATH_ACCENT, t.accents.math);

	if (t.retro)
	{
		// Full palette reset first (covers every colour the other themes may
		// have touched), then the CRT overrides.
		ImGui::StyleColorsDark();
		ImPlot::StyleColorsDark();
		const ImVec4 popup(t.bg.x + 0.03f, t.bg.y + 0.03f, t.bg.z + 0.03f, 0.98f);
		colors[ImGuiCol_WindowBg] = t.bg;
		colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
		colors[ImGuiCol_PopupBg] = popup;
		colors[ImGuiCol_Text] = t.text;
		colors[ImGuiCol_TextDisabled] = t.dim;
		colors[ImGuiCol_TextSelectedBg] = A(t.line, 0.35f);
		colors[ImGuiCol_Border] = A(t.lineDim, 0.85f);
		colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
		colors[ImGuiCol_FrameBg] = t.fill;
		colors[ImGuiCol_FrameBgHovered] = A(t.line, 0.22f);
		colors[ImGuiCol_FrameBgActive] = A(t.line, 0.32f);
		colors[ImGuiCol_TitleBg] = t.bg;
		colors[ImGuiCol_TitleBgActive] = popup;
		colors[ImGuiCol_TitleBgCollapsed] = t.bg;
		colors[ImGuiCol_MenuBarBg] = t.bg;
		colors[ImGuiCol_ScrollbarBg] = A(t.bg, 0.4f);
		colors[ImGuiCol_ScrollbarGrab] = A(t.lineDim, 0.45f);
		colors[ImGuiCol_ScrollbarGrabHovered] = A(t.line, 0.7f);
		colors[ImGuiCol_ScrollbarGrabActive] = t.line;
		colors[ImGuiCol_CheckMark] = t.line;
		colors[ImGuiCol_SliderGrab] = A(t.line, 0.55f);
		colors[ImGuiCol_SliderGrabActive] = t.line;
		colors[ImGuiCol_Button] = A(t.line, 0.10f);
		colors[ImGuiCol_ButtonHovered] = A(t.line, 0.25f);
		colors[ImGuiCol_ButtonActive] = A(t.line, 0.40f);
		colors[ImGuiCol_Header] = A(t.line, 0.12f);
		colors[ImGuiCol_HeaderHovered] = A(t.line, 0.24f);
		colors[ImGuiCol_HeaderActive] = A(t.line, 0.32f);
		colors[ImGuiCol_Separator] = A(t.lineDim, 1.0f);
		colors[ImGuiCol_SeparatorHovered] = A(t.line, 0.8f);
		colors[ImGuiCol_SeparatorActive] = t.line;
		colors[ImGuiCol_ResizeGrip] = A(t.line, 0.2f);
		colors[ImGuiCol_ResizeGripHovered] = A(t.line, 0.5f);
		colors[ImGuiCol_ResizeGripActive] = t.line;
		colors[ImGuiCol_Tab] = t.bg;
		colors[ImGuiCol_TabHovered] = A(t.line, 0.30f);
		colors[ImGuiCol_TabActive] = A(t.line, 0.22f);

		// Square corners + 1px-outlined widgets: the vector-CRT signature.
		style.FrameBorderSize = 1.0f;
		style.ChildRounding = 0.0f;
		style.FrameRounding = 0.0f;
		style.PopupRounding = 0.0f;
		style.GrabRounding = 0.0f;
		style.TabRounding = 0.0f;
		style.WindowRounding = 0.0f;
		style.ScrollbarRounding = 0.0f;

		// The plot becomes the CRT face: opaque screen, phosphor graticule.
		ImVec4* plot_colors = ImPlot::GetStyle().Colors;
		plot_colors[ImPlotCol_FrameBg] = ImVec4(0, 0, 0, 0);
		plot_colors[ImPlotCol_PlotBg] = t.screen;
		plot_colors[ImPlotCol_PlotBorder] = ImVec4(0, 0, 0, 0);
		plot_colors[ImPlotCol_AxisGrid] = t.grid;
		plot_colors[ImPlotCol_InlayText] = t.dim;
		plot_colors[ImPlotCol_Selection] = t.line;
		plot_colors[ImPlotCol_Crosshairs] = t.line;
		return;
	}

	// Classic themes keep their original rounding, with outlined widgets
	// (borders came in with the CRT family and read well here too).
	style.FrameBorderSize = 1.0f;
	style.ChildRounding = 5.0f * sc;
	style.FrameRounding = 0.0f;
	style.PopupRounding = 0.0f;
	style.GrabRounding = 0.0f;
	style.TabRounding = 4.0f * sc;
	style.WindowRounding = 0.0f;
	style.ScrollbarRounding = 9.0f * sc;

	if (!t.light)
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

	// Plot chrome shared by both classic themes. The plot background stays
	// transparent so plots inherit WindowBg (as the Monash app did); the grid
	// and inlay text flip contrast with the theme. PreviewStyle() mutates
	// these same globals mid-frame, so it uses identical values.
	ImVec4* plot_colors = ImPlot::GetStyle().Colors;
	plot_colors[ImPlotCol_FrameBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	plot_colors[ImPlotCol_PlotBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	plot_colors[ImPlotCol_PlotBorder] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	plot_colors[ImPlotCol_AxisGrid] = t.grid;
	plot_colors[ImPlotCol_InlayText] = t.light ? ImVec4(0.0f, 0.0f, 0.0f, 0.5f)
	                                           : ImVec4(1.0f, 1.0f, 1.0f, 0.5f);
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
	// Grid/inlay contrast follows the theme; values match SetGlobalStyle
	// (these globals leak past the preview, so keep the two in agreement).
	const ThemeSpec& t = CurrentTheme();
	colors[ImPlotCol_InlayText] = t.retro ? t.dim
	    : t.light                         ? ImVec4(0.0f, 0.0f, 0.0f, 0.5f)
	                                      : ImVec4(1.0f, 1.0f, 1.0f, 0.5f);
	//colors[ImPlotCol_AxisText] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
	colors[ImPlotCol_AxisGrid] = t.grid;
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
// One channel's capture is perfectly constant (every sample bit-identical).
// *known is false when the channel has no data to judge (e.g. CH2 in a
// single-channel mode).
static bool ChannelPerfectlyFlat(int channel, bool* known)
{
	const double time_window = 0.5;
	const double sample_rate_hz = 30;

	std::vector<double>* data = librador_get_analog_data_by_rate(
		channel, time_window, sample_rate_hz, 0, 0);
	if (data == nullptr || data->empty())
	{
		*known = false;
		return false;
	}
	*known = true;

	double first = (*data)[0];
	for (double v : *data)
	{
		if (v != first)
			return false; // not constant
	}
	return true;
}

bool CheckIfInUninitialisedMode()
{
	// A device wedged in an incomplete startup state flattens EVERYTHING it
	// captures, so require BOTH scope channels to be perfectly constant. A
	// single flat channel is an input condition, not a wedge — a quiet DC
	// source, or the hardware gain set high enough to rail that channel's
	// ADC (both false-triggered this warning when only CH1 was checked).
	bool ch1_known = false;
	if (!ChannelPerfectlyFlat(1, &ch1_known) || !ch1_known)
		return false;

	bool ch2_known = false;
	const bool ch2_flat = ChannelPerfectlyFlat(2, &ch2_known);
	// In single-channel modes CH2 can't vote; CH1 alone decides, as before.
	return ch2_known ? ch2_flat : true;
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




