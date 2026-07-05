#ifndef UTIL_H
#define UTIL_H

#include <map>
#include "imgui.h"
#include <string>
#include "implot.h"
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#define _USE_MATH_DEFINES
#include "math.h"
#include "exprtk.hpp"
#include <chrono>
#include <string>
#include <unordered_map>
#include <iostream>
#include "librador.h"
#include "platform/paths.h"

#define MATH_CUSTOM 15


namespace constants
{
// Desired Firmware Version/Variant
constexpr uint16_t DESIRED_FW_VERSION = 0x000A;
constexpr uint8_t DESIRED_FW_VARIANT = 3;

// Theme Colours
constexpr ImU32 PRIM_LIGHT = IM_COL32(255, 255, 255, 255); // primary light
// Secondary/hint text. Not constexpr: SetGlobalStyle retints it per theme
// (dim phosphor in the CRT themes, neutral gray in the classic ones).
extern ImVec4 GRAY_TEXT;

// Per-instrument chrome accents. Not constexpr: widgets hold live pointers
// to these arrays, and SetGlobalStyle rewrites them per theme — the mono CRT
// themes collapse them all to the theme line colour, "Arcade" installs a
// neon palette, the classic themes restore the original Monash colours.
extern float SG1_ACCENT[3];
extern float SG2_ACCENT[3];
extern float PSU_ACCENT[3];
extern float OSC_ACCENT[3];
extern float GEN_ACCENT[3];
extern float SPECTRUM_ANALYSER_ACCENT[3];
extern float NETWORK_ANALYSER_ACCENT[3];

// Trace/data colours. Also theme-written, but every theme keeps the same hue
// family (CH1 = yellow-ish, CH2 = cyan-ish, matching the Qt app's
// Qt::yellow/Qt::cyan) so channels stay identifiable — only the light theme
// darkens them for contrast on a white plot.
extern float OSC1_ACCENT[3];
extern float OSC2_ACCENT[3];
extern float MATH_ACCENT[3];
constexpr float PLOT_ACCENT[3] = {0., 0., 0.};

// Syntax Highlighting Colours
constexpr ImU32 SymbolColour = IM_COL32(175, 253, 255, 255);
constexpr ImU32 FunctionColour = IM_COL32(78, 201, 176, 255);
constexpr ImU32 NumberColour = IM_COL32(181, 206, 168, 255);
constexpr ImU32 KeywordColour = IM_COL32(86,156,214,255);


// Signal Generator Preview Waves
constexpr const char* wavetypes[4] = { "Sine", "Square", "Sawtooth", "Triangle" };
constexpr int PREVIEW_RES = 256;
extern float x_preview[PREVIEW_RES+1];
extern float sine_preview[PREVIEW_RES+1];
extern float square_preview[PREVIEW_RES+1];
extern float sawtooth_preview[PREVIEW_RES+1];
extern float triangle_preview[PREVIEW_RES+1];

// Frequency Units
static std::vector<std::string> freq_prefs = { "", "k" };
// Frequency formats
static std::vector<std::string> freq_formats = { "%.1f", "%.0f" };

// Voltage Units
static std::vector<std::string> volt_prefs = { "", "m" };
// Voltage Units
static std::vector<std::string> volt_formats = { "%.2f", "%.0f" };

// Phase Units
static std::vector<std::string> phase_prefs = { "" };
// Phase Units
static std::vector<std::string> phase_formats = { "%.0f" };


//  textures
extern int pinout_width;
extern int pinout_height;
extern int glob_pinout_width;
extern int glob_pinout_height;
extern intptr_t psu_pinout_texture;
extern intptr_t sg_pinout_texture;
extern intptr_t osc_pinout_texture;
extern intptr_t glob_pinout_texture;
// Trigger Type
enum TriggerType
{
	RISING_EDGE,
	FALLING_EDGE
};
enum Channel
{
	OSC1 = 1,
	OSC2 = 2
};
enum LineType
{
	Vertical,
	Horizontal
};


// Sample Rate Divisor List
const std::vector<int> DIVISORS_375000 = { 1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 24, 25,
	30, 40, 50, 60, 75, 100, 120, 125, 150, 200, 250, 300, 375, 500, 600, 625, 750, 1000,
	1250, 1500, 1875, 2500, 3000, 3125, 3750, 5000, 6250, 7500, 9375, 12500, 15000, 15625,
	18750, 25000, 31250, 37500, 46875, 62500, 75000, 93750, 125000, 187500, 375000 };
}

/// <summary>
/// Represents dropdrown menu for help documentation.
/// </summary>
struct TreeNode
{
	std::string name;
	std::vector<std::string> bullets;
	std::vector<TreeNode> children;
	bool expanded;
	bool isFound(std::string search)
	{
		bool found = false;
		for (const std::string s : bullets)
		{
			found |= s.find(search) != std::string::npos;
			if (found) break;
		}
		return found;
	};
};
namespace maps
{
class ChannelTriggerPair
{
public:
	constants::Channel channel;
	constants::TriggerType trigger_type;
	ChannelTriggerPair(constants::Channel channel, constants::TriggerType trigger_type)
	{
		this->channel = channel;
		this->trigger_type = trigger_type;
	}
};
// map between trigger type combo item number, and channel trigger type pair
const std::map<int, maps::ChannelTriggerPair> ComboItemToChannelTriggerPair = {
	{ 0, maps::ChannelTriggerPair(constants::Channel::OSC1, constants::TriggerType::RISING_EDGE) },
	{ 1, maps::ChannelTriggerPair(constants::Channel::OSC1, constants::TriggerType::FALLING_EDGE) },
	{ 2, maps::ChannelTriggerPair(constants::Channel::OSC2, constants::TriggerType::RISING_EDGE) },
	{ 3, maps::ChannelTriggerPair(constants::Channel::OSC2, constants::TriggerType::FALLING_EDGE) }
};
}

enum PlotColour {
	Plot_Red,
	Plot_Green,
	Plot_Blue,
	Plot_Yellow,
	Plot_Cyan,
	Plot_Magenta,
	Plot_White
};
struct ParseStatus {
	bool success = false;
	bool osc1_empty_causing_parse_error;
	bool osc2_empty_causing_parse_error;
};

void init_constants();
void PreviewStyle();
void BeginControlWidgetStyle(const float ac[3]);
void EndControlWidgetStyle();

// ---- Themes -----------------------------------------------------------------
// A UI theme. The `retro` family ("Phosphor" and its derivations) is the CRT
// look: near-black background, chrome (segment frames, outlines, headers)
// drawn in `line`, plots become a phosphor screen (`screen` + `grid`), square
// corners and 1px-outlined widgets everywhere; the desktop layout adds a
// vector bezel and scanlines on top. `mono` retro themes additionally replace
// the per-instrument accent colours with `line` for a single-phosphor look;
// non-mono retro ("Arcade") keeps the accents for a colour-CRT vibe.
// Per-instrument accent palette (RGB triplets matching the float[3] accent
// pointers the widgets hold). SetGlobalStyle copies these over the mutable
// constants:: accent arrays, so a colourful theme can give the scope, logic
// analyzer, PSU etc. distinct hues while the mono themes collapse them all
// to the phosphor line colour.
struct ThemeAccents
{
	float psu[3];    // power supply
	float sg1[3];    // signal generator 1 (+ digital outputs)
	float sg2[3];    // signal generator 2 (+ calibration)
	float gen[3];    // generic toggles: trigger, cursors, XY...
	float logic[3];  // logic analyzer / spectrum analyser
	float record[3]; // DAQ / network analyser
	float scope[3];  // scope chrome (plot settings box, Scope panel)
	// Trace colours: same hue family in every theme (CH1 yellow-ish, CH2
	// cyan-ish); the light theme darkens them for contrast on white.
	float ch1[3];
	float ch2[3];
	float math[3];
};

struct ThemeSpec
{
	const char* id;    // settings.ini value
	const char* label; // menu label
	bool retro;        // phosphor/CRT family
	bool mono;         // retro only: chrome accents collapse to `line`
	bool light;        // classic light palette (non-retro only)
	bool pixelFont;    // "Retro" variants: VT323 terminal font (else Rajdhani)
	ImVec4 bg;         // window background
	ImVec4 text;       // primary text
	ImVec4 dim;        // secondary text
	ImVec4 line;       // bright chrome: segment frames, outlines, headers
	ImVec4 lineDim;    // subdued lines: separators, inner borders
	ImVec4 fill;       // widget fill (FrameBg)
	ImVec4 screen;     // plot background (the CRT face)
	ImVec4 grid;       // plot grid / graticule
	ThemeAccents accents; // per-instrument chrome palette
};

int ThemeCount();
const ThemeSpec& ThemeAt(int idx);
const ThemeSpec* FindTheme(const std::string& id); // nullptr if unknown
const ThemeSpec& CurrentTheme(); // last theme applied by SetGlobalStyle

// Applies the app-wide theme (called every frame from App::Update).
void SetGlobalStyle(const ThemeSpec& theme);
ImU32 colourConvert(const float c[3], float alpha = 1.0f);
void replace_all(
    std::string& s, std::string const& toReplace, std::string const& replaceWith);
void MultiplyButtonColour(ImU32* ButtonColour, float multiplier);
std::string NumToString(double num,int precision);
int MetricFormatter(double value, char* buff, int size, void* data);
void ToggleTriggerTypeComboChannel(int* ComboCurrentItem);
void ToggleTriggerTypeComboType(int* ComboCurrentItem);
std::vector<double> EvalUserExpression(std::string user_text, std::vector<double> osc1, std::vector<double> osc2, std::vector<double> time, ParseStatus& parse_status);
bool SliderFloatPercent(const char* label, float* v01,
	const char* fmt = "%.0f%%",
	ImGuiSliderFlags flags = 0);
std::vector<float> linspace(float x_min, float x_max, int resolution);
ImVec4 GetPlotColour(PlotColour c);
bool CheckIfInSafetyMode();
bool CheckIfInUninitialisedMode();
// Export Stuff
struct ExportRowState {
	int  destComboIdx = 0;     // 0 = clipboard, 1 = csv
	bool copiedFlag = false; // triggers status flash
	int  copiedFrame = -1000;
	int  copiedLingerFrames = 50;

	bool lastWasClipboard = true;  // NEW: true = "Copied!", false = "Saved!"
};

std::string BuildDelimited2Col(const std::vector<double>& x,
	const std::vector<double>& y,
	const char* xHeader,
	const char* yHeader,
	char sep);

bool Export2ColToClipboard(const std::vector<double>& x,
	const std::vector<double>& y,
	const char* xHeader,
	const char* yHeader);


bool Export2ColToCsvFile(const char* basePath,
	const char* fileExtension,
	const std::vector<double>& x,
	const std::vector<double>& y,
	const char* xHeader,
	const char* yHeader);

struct PlotTrace {
	std::vector<double> x = {};
	std::vector<double> y = {};
};
struct SpectrumPlots {
	PlotTrace osc1;
	PlotTrace osc2;
	// later: math, averaged, etc.
};
struct NetworkPlots {
	std::vector<double> freq = {};
	std::vector<double> mag = {};
	std::vector<double> phase = {};
};
#endif