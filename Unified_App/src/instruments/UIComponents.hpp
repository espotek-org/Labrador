#pragma once
#ifndef _UI_H_
#define _UI_H_

#include "imgui.h"
#include "util.h"
#include <iostream>
#include "AcquireState.hpp"
#include "NetworkAnalyser.hpp"
#include <chrono>
#include "platform/file_dialog.h"

/// <summary>
/// Adapted from https://github.com/ocornut/imgui/issues/1537#issuecomment-355562097
/// </summary>
/// <param name="id"></param>
/// <param name="state"></param>
/// <param name="accentColour"></param>
bool inline ToggleSwitch(const char* id, bool* state, ImU32 accentColour)
	{
		ImVec2 p = ImGui::GetCursorScreenPos();
		ImDrawList* draw_list = ImGui::GetWindowDrawList();

		float height = ImGui::GetFrameHeight() * 0.8;
		float width = height * 1.8f;
		float switch_width = width * 0.30f;
	    float pad = 2.0f;
	    float rounding = 0.0f;
	    bool switched = false;
	    if (ImGui::InvisibleButton(id, ImVec2(width, height)))
	    {
		    *state = !*state;
		    switched = true;
		}

		// CRT themes: OFF is a dark outlined well (unlit phosphor), the knob
		// glows in the text colour; classic themes keep the gray/white look.
		const ThemeSpec& th = CurrentTheme();
		ImU32 col_off = th.retro ? ImGui::ColorConvertFloat4ToU32(th.fill)
		                         : IM_COL32(150, 150, 150, 255);
		ImU32 col_knob = th.retro ? ImGui::ColorConvertFloat4ToU32(th.text)
		                          : constants::PRIM_LIGHT;
		ImU32 col_bg = *state ? accentColour : col_off;

		draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, rounding);

		draw_list->AddRectFilled(
			*state ?	ImVec2(p.x + width - pad - switch_width,	p.y + pad) : // ON top-left
						ImVec2(p.x + pad,							p.y + pad),  // OFF top-left
	        *state ?	ImVec2(p.x + width - pad,					p.y + height - pad) : // ON bottom-right
						ImVec2(p.x + pad + switch_width,			p.y + height - pad),  // OFF bottom-right
			col_knob,
			rounding);

		// 1px outline, matching the outlined widgets everywhere
		draw_list->AddRect(p, ImVec2(p.x + width, p.y + height),
		    ImGui::ColorConvertFloat4ToU32(th.lineDim), rounding);

		return switched;
	}


/// <summary>
/// Adapted from https://github.com/ocornut/imgui/issues/1537#issuecomment-355562097
/// </summary>
/// <param name="id"></param>
/// <param name="state"></param>
/// <param name="accentColour"></param>
bool inline ToggleButton(const char* id, ImVec2 size, bool* state, ImU32 trueColour, ImU32 falseColour)
{
	ImVec2 p = ImGui::GetCursorScreenPos();
	ImDrawList* draw_list = ImGui::GetWindowDrawList();

	ImU32 ButtonColour = *state ? trueColour : falseColour;

	float height = 30;
	float width = 100;

	float rounding = 0.0f;
	bool switched = false;
	if (ImGui::InvisibleButton(id, ImVec2(width, height)))
	{
		*state = !*state;
		switched = true;
	}

	if (ImGui::IsItemActive())
	{
		MultiplyButtonColour(&ButtonColour, 0.8);
	}
	else if (ImGui::IsItemHovered())
	{
		MultiplyButtonColour(&ButtonColour, 1.25);
	}


	draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ButtonColour, rounding);
	ImVec2 text_size = ImGui::CalcTextSize(id);
	ImVec2 text_p
	    = ImVec2(p.x + width/2 - text_size.x / 2, p.y + height/2 - text_size.y / 2);
	draw_list->AddText(text_p, IM_COL32(255, 255, 255, 255), id);


	return switched;
}

/// <summary>
/// Generic dropdown menu for list of objects
/// </summary>
/// <typeparam name="T">Has getLabel() method</typeparam>
/// <param name="id"></param>
/// <param name="options">Dropdown options</param>
/// <param name="active">Active index</param>
/// <param name="size">Length of options</param>
/// <returns></returns>
template <typename T>
    bool inline ObjectDropDown(const char* id, T const options[], int* active, int size)
    {
	bool changed = false;
	std::string preview = options[*active]->getLabel();
	// std::cout << "size: " << size;
	if (ImGui::BeginCombo(id, preview.c_str(), 0))
	{
		for (int n = 0; n < size; n++)
		{
			const bool is_selected = (*active == n);
			if (ImGui::Selectable((options[n]->getLabel()).c_str(), is_selected))
			{
				*active = n;
				changed = true;
			}
			
			// Set the initial focus when opening the combo (scrolling + keyboard
			// navigation focus)
			if (is_selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	return changed;
}

/// <summary>
/// Generic dropdown for list of strings
/// </summary>
/// <param name="id"></param>
/// <param name="options"></param>
/// <param name="active"></param>
/// <param name="size"></param>
/// <returns></returns>
int inline DropDown(const char* id, std::vector<std::string> options, int* active)
{
	int changed = 0;
	const char* preview = options[*active].c_str();
	if (ImGui::BeginCombo(id, preview, ImGuiComboFlags_NoArrowButton))
	{
		for (int n = 0; n < options.size(); n++)
		{
			const bool is_selected = (*active == n);
			if (ImGui::Selectable(options[n].c_str(), is_selected))
			{
				*active = n;
				changed = 1;
			}

			// Set the initial focus when opening the combo (scrolling + keyboard
			// navigation focus)
			if (is_selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	return changed;
}

void inline TextRight(std::string text)
{
	auto windowWidth = ImGui::GetWindowSize().x;
	auto textWidth = ImGui::CalcTextSize(text.c_str()).x;

	ImGui::SetCursorPosX(windowWidth - textWidth - 10);
	ImGui::Text("%s", text.c_str());
}

class SIValue
{
public:
	/// <summary>
	/// Handle SI Values, eg Voltage values, time values
	/// </summary>
	/// <param name="id">Unique id for this value</param>
	/// <param name="label"></param>
	/// <param name="default_val">Default state</param>
	/// <param name="min_val">Minimum bound</param>
	/// <param name="max_val">Maximum bound</param>
	/// <param name="symbol">SI Unit Symbol. Eg. V for Volts</param>
	/// <param name="prefixs">Allowable prefixes eg {"m", "", "k"}</param>
	/// <param name="format">List of c formatting string eg ".2f" for each prefix<std::string></param>
	SIValue(std::string id, std::string label, float default_val, float min_val, float max_val, std::string symbol, std::vector<std::string> prefixs,
	    std::vector<std::string> format)
	    : id(id)
		, label(label)
	    , value(default_val)
	    , min_val(min_val)
	    , max_val(max_val)
	    , symbol(symbol)
	    , prefixs(prefixs)
	    , formats(format)
	{
		updateDisplayValue();
		for (int i = 0; i < prefixs.size(); i++)
		{
			options.push_back(prefixs[i] + symbol);
		}
	}

	bool renderInTable(float inpWidth)
	{
		bool changed = false;
		ImGui::TableNextColumn();
		ImGui::Text("%s", label.c_str());
		ImGui::TableNextColumn();
		ImGui::SetNextItemWidth(inpWidth);
		changed |= ImGui::DragFloat(
			id.c_str(), 
			&display_value, 
			label == "Phase" ? 1.0f :
			std::abs(display_value) < 0.01 ? 0.01 : 0.01f * std::abs(display_value), // variable speed slider
			display_min_val, 
			display_max_val, 
			getFormat(), 
			ImGuiSliderFlags_AlwaysClamp // clamp even with manual input
		);
		if (ImGui::IsItemHovered() && GImGui->HoveredIdTimer > 0.6f)
		{
			ImGui::SetTooltip("Double click to type value.\nClick+drag to adjust value.");
		}

		ImGui::SameLine();
		if (options.size() > 1 && DropDown(("##" + id + "dd").c_str(), options, &prefix_idx))
		{
			updateDisplayValue();
		}
		else if (options.size() == 1) ImGui::Text("%s", options[0].c_str());
		// ImGui::TableNextColumn();
		if (changed) updateValue();
		return changed;
	}

	float getValue()
	{
		return value;
	}

	void setLevel(float newValue)
	{
		value = newValue;
		updateDisplayValue();
	}

private:
	const std::string label;
	const std::string id;
	float value; // SI value (always in standard unit, eg volts)
	float display_value; // Value to display with unit conversion
	float display_max_val;
	float display_min_val;
	float min_val;
	float max_val;
	std::string symbol;
	std::vector<std::string> formats;
	int prefix_idx = 0;
	std::vector<std::string> prefixs;
	std::vector<std::string> options;
	int n_prefix;

	/// <summary>
	/// Multiplier to convert prefixed SI unit to standard SI unit, eg mV -> V
	/// </summary>
	/// <returns></returns>
	float getMutliplier()
	{
		std::string prefix = prefixs[prefix_idx];
		if (prefix.empty()) return 1;
		else if (!prefix.compare("m")) return 1e-3;
		else if (!prefix.compare("k")) return 1e3;
		else return 1;
	}

	const char* getFormat()
	{
		return (formats[prefix_idx]).c_str();
	}

	void updateDisplayValue()
	{
		const float m = getMutliplier();
		display_value = value / m;
		display_min_val = min_val / m;
		display_max_val = max_val / m;
	}
	void updateValue()
	{
		value = display_value * getMutliplier();
	}

};

// Outlined action button. Historically a white outline; the border now
// follows the theme (phosphor line in the CRT themes, black in the light one).
bool inline WhiteOutlineButton(const char* id, ImVec2 size=ImVec2(0, 0))
{
	const ThemeSpec& th = CurrentTheme();
	const ImVec4 border = th.retro ? th.line
	    : th.light                 ? ImVec4(0, 0, 0, 0.5f)
	                               : ImVec4(1, 1, 1, 0.5f);
	ImGui::PushStyleColor(ImGuiCol_Border, border);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1);
	bool result = ImGui::Button(id, size);
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
	return result;
}

static bool DrawAcquireButton(SpectrumAcquireState& st, const char* base_label = "Start Acquiring") {
	double elapsed = 0.0;
	if (st.acquiring) {
		elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - st.t0).count();
		if (elapsed >= st.max_time_s) {
			st.acquiring = false;
			st.elapsed_last_s = st.max_time_s;
			st.success_until_time = ImGui::GetTime() + 1.2;
		}
	}

	static float kAcquireBtnWidth = 0.0f;
	{
		const ImGuiStyle& s = ImGui::GetStyle();
		float w1 = ImGui::CalcTextSize(base_label).x;
		float w2 = ImGui::CalcTextSize("Acquiring: 9999.9 s").x;
		float w3 = ImGui::CalcTextSize(u8"\u2713  9999.99 s").x;
		float need = ImMax(ImMax(w1, w2), w3) + s.FramePadding.x * 2.0f + 8.0f;
		if (need > kAcquireBtnWidth) kAcquireBtnWidth = need;
	}

	bool pressed = WhiteOutlineButton("##AcquireBtn", ImVec2(kAcquireBtnWidth, 30));
	ImVec2 rmin = ImGui::GetItemRectMin();
	ImVec2 rmax = ImGui::GetItemRectMax();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	float rounding = ImGui::GetStyle().FrameRounding;

	if (st.acquiring) {
		float t = (float)std::min(elapsed / st.max_time_s, 1.0);
		ImU32 col = ImGui::GetColorU32(ImVec4(0.30f, 0.75f, 0.40f, 0.35f));
		ImVec2 a = ImVec2(rmin.x + 1.0f, rmin.y + 1.0f);
		ImVec2 b = ImVec2(rmin.x + 1.0f + t * (rmax.x - rmin.x - 2.0f), rmax.y - 1.0f);
		dl->AddRectFilled(a, b, col, rounding);
	}

	const double now_ui = ImGui::GetTime();
	const bool show_success = (!st.acquiring && now_ui < st.success_until_time);

	char label_buf[96];
	ImU32 txt_col = ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_Text]);
	if (show_success) {
		std::snprintf(label_buf, sizeof(label_buf), u8"\u2713  %.2f s", st.elapsed_last_s);
		txt_col = ImGui::GetColorU32(ImVec4(0.20f, 0.90f, 0.40f, 1.0f));
	}
	else if (st.acquiring) {
		std::snprintf(label_buf, sizeof(label_buf), "Acquiring: %.1f s", elapsed);
	}
	else {
		std::snprintf(label_buf, sizeof(label_buf), "%s", base_label);
	}

	ImVec2 ts = ImGui::CalcTextSize(label_buf);
	ImVec2 c = ImVec2((rmin.x + rmax.x - ts.x) * 0.5f, (rmin.y + rmax.y - ts.y) * 0.5f);
	dl->AddText(c, txt_col, label_buf);

	bool do_capture = false;
	if (pressed) {
		if (!st.acquiring) {
			st.acquiring = true; st.t0 = std::chrono::steady_clock::now();
		}
		else {
			st.acquiring = false;
			st.elapsed_last_s = std::max(0.0, std::chrono::duration<double>(std::chrono::steady_clock::now() - st.t0).count());
			st.success_until_time = ImGui::GetTime() + 1.2;
			do_capture = true;
		}
	}
	if (!st.acquiring && elapsed >= st.max_time_s && st.elapsed_last_s > 0.0) do_capture = true;
	return do_capture;
}

// Log-range slider with default-SliderFloat height, rectangular grabs,
	// and value labels shown ABOVE the slider (like your original).
inline bool RangeSliderDoubleLog(const char* label,
	double* v_lower, double* v_upper,
	double v_min, double v_max,
	double min_span_hz = 0.0, bool auto_swap = true, bool snap_125_on_rel = false,
	float widget_width = 0.0f, float bar_thickness_px = 4.0f, float /*knob_radius_px*/ = 7.0f,
	float top_label_margin_px = 4.0f, float left_pad_px = 0.0f, float right_pad_px = 16.0f)
{
	IM_ASSERT(v_lower && v_upper);
	if (!(v_min > 0.0 && v_min < v_max)) return false;

	// ---- helpers (local; no redefinitions) ----
	auto clampd = [](double x, double a, double b) { return x < a ? a : (x > b ? b : x); };

	auto snap125 = [](double x)->double {
		if (x <= 0) return x;
		double e = std::floor(std::log10(x));
		double m = x / std::pow(10.0, e);
		double t = (m < 1.5) ? 1.0 : (m < 3.5 ? 2.0 : (m < 7.5 ? 5.0 : 10.0));
		return t * std::pow(10.0, e);
	};

	auto format_hz_si = [](char* buf, size_t n, double hz) {
		const char* unit = "Hz"; double v = hz;
		if (hz >= 1e9) { v = hz / 1e9; unit = "GHz"; }
		else if (hz >= 1e6) { v = hz / 1e6; unit = "MHz"; }
		else if (hz >= 1e3) { v = hz / 1e3; unit = "kHz"; }
		std::snprintf(buf, n, "%.4g %s", v, unit);
	};

	struct LogMap {
		double lmin, lmax, inv_span;
		LogMap(double vmin, double vmax) {
			lmin = std::log10(vmin); lmax = std::log10(vmax);
			inv_span = 1.0 / (lmax - lmin);
		}
		double v_to_t(double v) const { return (std::log10(v) - lmin) * inv_span; }
		double t_to_v(double t) const { return std::pow(10.0, lmin + (lmax - lmin) * t); }
	};

	// ---- inputs (clamped and constrained) ----
	double lo = clampd(*v_lower, v_min, v_max);
	double hi = clampd(*v_upper, v_min, v_max);

	auto apply_constraints = [&](double& a, double& b) {
		a = clampd(a, v_min, v_max);
		b = clampd(b, v_min, v_max);
		if (auto_swap) {
			if (a > b) std::swap(a, b);
			if (min_span_hz > 0 && b - a < min_span_hz)
				b = clampd(a + min_span_hz, v_min, v_max);
		}
		else {
			if (min_span_hz > 0) {
				if (a > b - min_span_hz) a = b - min_span_hz;
				if (b < a + min_span_hz) b = a + min_span_hz;
			}
			a = clampd(a, v_min, v_max);
			b = clampd(b, v_min, v_max);
		}
	};
	apply_constraints(lo, hi);

	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems) { *v_lower = lo; *v_upper = hi; return false; }

	const ImGuiStyle& style = ImGui::GetStyle();
	const ImGuiID id = ImGui::GetID(label);

	ImGui::BeginGroup();
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(label);
	ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);

	// --- first label pass (to reserve top text height) ---
	char lbl_lo[32], lbl_hi[32];
	format_hz_si(lbl_lo, sizeof(lbl_lo), lo);
	format_hz_si(lbl_hi, sizeof(lbl_hi), hi);
	ImVec2 sz_lo = ImGui::CalcTextSize(lbl_lo);
	ImVec2 sz_hi = ImGui::CalcTextSize(lbl_hi);
	const float labels_h = (sz_lo.y > sz_hi.y ? sz_lo.y : sz_hi.y);

	// Width planning
	float avail = ImGui::GetContentRegionAvail().x;
	float inner_w = (widget_width > 0.0f ? widget_width : avail);
	inner_w = ImMax(1.0f, inner_w - (left_pad_px + right_pad_px));

	// Height: match default ImGui slider height
	const float frame_h = ImGui::GetFrameHeight();        // <- matches SliderFloat height
	const float widget_h = labels_h + top_label_margin_px + frame_h;

	// Bounding box
	ImVec2 pos = ImGui::GetCursorScreenPos(); pos.x += left_pad_px;
	ImVec2 size(inner_w, widget_h);
	ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
	ImGui::ItemSize(size, style.FramePadding.y);
	if (!ImGui::ItemAdd(bb, id)) { *v_lower = lo; *v_upper = hi; ImGui::EndGroup(); return false; }

	// Geometry for frame (slider body)
	const float rounding = style.FrameRounding;
	const float border_size = style.FrameBorderSize;
	ImRect frame_rect(
		ImVec2(bb.Min.x, bb.Max.y - frame_h), // bottom area is the slider body
		ImVec2(bb.Max.x, bb.Max.y)
	);

	// Usable bar inside the frame: leave margins so grabs stay within frame
	const float grab_w = ImMax(style.GrabMinSize, 6.0f);
	const float bar_x0 = frame_rect.Min.x + grab_w * 0.5f;
	const float bar_x1 = frame_rect.Max.x - grab_w * 0.5f;
	const float bar_w = ImMax(1.0f, bar_x1 - bar_x0);

	// Log mapping
	LogMap lm(v_min, v_max);
	auto v_to_x = [&](double v)->float { return (float)(bar_x0 + lm.v_to_t(v) * bar_w); };
	auto x_to_v = [&](float x)->double { return lm.t_to_v(clampd((x - bar_x0) / bar_w, 0.0, 1.0)); };

	float x_lo = v_to_x(lo), x_hi = v_to_x(hi);

	// Interactives FIRST (so we know active/hover to choose colors)
	ImGui::PushID(id);

	ImRect lo_rect(ImVec2(x_lo - grab_w * 0.5f, frame_rect.Min.y),
		ImVec2(x_lo + grab_w * 0.5f, frame_rect.Max.y));
	ImGui::SetCursorScreenPos(lo_rect.Min);
	ImGui::InvisibleButton("lo", lo_rect.GetSize());
	bool lo_active = ImGui::IsItemActive();
	bool lo_hover = ImGui::IsItemHovered();

	ImRect hi_rect(ImVec2(x_hi - grab_w * 0.5f, frame_rect.Min.y),
		ImVec2(x_hi + grab_w * 0.5f, frame_rect.Max.y));
	ImGui::SetCursorScreenPos(hi_rect.Min);
	ImGui::InvisibleButton("hi", hi_rect.GetSize());
	bool hi_active = ImGui::IsItemActive();
	bool hi_hover = ImGui::IsItemHovered();

	// Click anywhere on the frame to pick nearest handle
	if (!lo_active && !hi_active && ImGui::IsMouseClicked(0) &&
		ImGui::IsMouseHoveringRect(frame_rect.Min, frame_rect.Max))
	{
		float mx = ImGui::GetIO().MousePos.x;
		if (fabsf(mx - x_lo) <= fabsf(mx - x_hi)) { ImGui::SetActiveID(ImGui::GetID("lo"), window); lo_active = true; }
		else { ImGui::SetActiveID(ImGui::GetID("hi"), window); hi_active = true; }
	}

	// Choose frame color like SliderFloat
	ImU32 col_frame = ImGui::GetColorU32(ImGuiCol_FrameBg);
	if (ImGui::IsMouseHoveringRect(frame_rect.Min, frame_rect.Max))
		col_frame = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
	if (lo_active || hi_active)
		col_frame = ImGui::GetColorU32(ImGuiCol_FrameBgActive);

	// Draw frame
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(frame_rect.Min, frame_rect.Max, col_frame, rounding);
	if (border_size > 0.0f) {
		const ImU32 col_border = ImGui::GetColorU32(ImGuiCol_Border);
		dl->AddRect(frame_rect.Min, frame_rect.Max, col_border, rounding, 0, border_size);
	}

	// Within the frame: draw the thin track and the selected range
	const float track_cy = 0.5f * (frame_rect.Min.y + frame_rect.Max.y);
	const float half_th = 0.5f * ImClamp(bar_thickness_px, 1.0f, frame_h); // keep reasonable
	const ImU32 col_bar_bg = ImGui::GetColorU32(ImGuiCol_FrameBg);
	const ImU32 col_bar_sel = ImGui::GetColorU32(ImGuiCol_SliderGrabActive);

	// track (full range)
	dl->AddRectFilled(
		ImVec2(bar_x0, track_cy - half_th),
		ImVec2(bar_x1, track_cy + half_th),
		col_bar_bg, half_th
	);

	// Update while dragging
	bool changed_now = false;
	if (lo_active) { lo = clampd(x_to_v(ImGui::GetIO().MousePos.x), v_min, v_max); apply_constraints(lo, hi); changed_now = true; }
	if (hi_active) { hi = clampd(x_to_v(ImGui::GetIO().MousePos.x), v_min, v_max); apply_constraints(lo, hi); changed_now = true; }

	// Recompute x positions
	x_lo = v_to_x(lo); x_hi = v_to_x(hi);

	// selected region
	float sel_x0 = ImMin(x_lo, x_hi);
	float sel_x1 = ImMax(x_lo, x_hi);
	dl->AddRectFilled(
		ImVec2(sel_x0, track_cy - half_th),
		ImVec2(sel_x1, track_cy + half_th),
		col_bar_sel, half_th
	);

	// Rectangular grabs (full frame height)
	const ImU32 col_grab = ImGui::GetColorU32(ImGuiCol_SliderGrab);
	const ImU32 col_grab_act = ImGui::GetColorU32(ImGuiCol_SliderGrabActive);
	const float grab_round = style.FrameRounding;

	auto draw_grab = [&](float cx, bool active, bool hover) {
		(void)hover;
		ImU32 col = active ? col_grab_act : col_grab;
		ImRect r(ImVec2(cx - grab_w * 0.5f, frame_rect.Min.y),
			ImVec2(cx + grab_w * 0.5f, frame_rect.Max.y));
		dl->AddRectFilled(r.Min, r.Max, col, grab_round);
		if (border_size > 0.0f) {
			const ImU32 col_b = ImGui::GetColorU32(ImGuiCol_Border);
			dl->AddRect(r.Min, r.Max, col_b, grab_round, 0, border_size * 0.5f);
		}
	};
	draw_grab(x_lo, lo_active || lo_hover, lo_hover);
	draw_grab(x_hi, hi_active || hi_hover, hi_hover);

	// ---- labels ABOVE the slider (like your original) ----
	format_hz_si(lbl_lo, sizeof(lbl_lo), lo);
	format_hz_si(lbl_hi, sizeof(lbl_hi), hi);
	sz_lo = ImGui::CalcTextSize(lbl_lo);  // reuse vars safely
	sz_hi = ImGui::CalcTextSize(lbl_hi);
	const ImU32 col_text = ImGui::GetColorU32(ImGuiCol_Text);
	const float text_baseline = bb.Min.y + labels_h; // top area is from bb.Min.y up to labels_h
	dl->AddText(ImVec2(x_lo - sz_lo.x * 0.5f, text_baseline - sz_lo.y), col_text, lbl_lo);
	dl->AddText(ImVec2(x_hi - sz_hi.x * 0.5f, text_baseline - sz_hi.y), col_text, lbl_hi);

	// Optional: snap on release
	static bool lo_was = false, hi_was = false;
	if (snap_125_on_rel) {
		if (lo_was && !lo_active && ImGui::IsMouseReleased(0)) { lo = snap125(lo); apply_constraints(lo, hi); changed_now = true; }
		if (hi_was && !hi_active && ImGui::IsMouseReleased(0)) { hi = snap125(hi); apply_constraints(lo, hi); changed_now = true; }
	}
	lo_was = lo_active; hi_was = hi_active;

	ImGui::PopID();
	ImGui::EndGroup();

	const bool any_change = changed_now || (lo != *v_lower) || (hi != *v_upper);
	*v_lower = lo; *v_upper = hi;
	return any_change;
}



static bool DrawNetworkAcquireButton(
	NetworkAcquireState& st, NetworkAnalyser* na, NetworkAnalyser::Config* cfg,
	ImVec2 size = ImVec2(200, 30))
{
	const double now = ImGui::GetTime();
	bool clicked = false;

	if (st.acquiring && na) {
		st.elapsed_last_s = now - st.t_start_s;
		if (na->done()) {
			st.acquiring = false;
			st.success_until_time = now + 1.2;
		}
	}

	char label[128];
	if (st.acquiring && na && na->CurrentConfig().points > 0) {
		const int total = na->CurrentConfig().points;
		const int done = std::clamp(na->current_index(), 0, total);
		std::snprintf(label, sizeof(label), "Acquiring: %d / %d (%.1f s)", done, total, st.elapsed_last_s);
		ImVec2 p_min = ImGui::GetCursorScreenPos();
		ImVec2 p_max = ImVec2(p_min.x + size.x, p_min.y + size.y);
		float w = (float)((p_max.x - p_min.x) * ((double)done / (double)total));
		ImGui::GetWindowDrawList()->AddRectFilled(p_min, ImVec2(p_min.x + w, p_max.y),
			IM_COL32(80, 255, 100, 120), ImGui::GetStyle().FrameRounding);
	}
	else if (now < st.success_until_time) {
		std::snprintf(label, sizeof(label), u8"Completed in %.1f s  \u2713", st.elapsed_last_s);
	}
	else {
		const double est = na ? na->EstimateSweepSeconds_UI(*cfg) : 0.0;
		std::snprintf(label, sizeof(label), "Acquire (est. %.0f s)", est);
	}

	if (WhiteOutlineButton(label, size)) {
		if (!st.acquiring && na) {
			na->StartSweep(*cfg);
			st.acquiring = true; st.t_start_s = now; st.elapsed_last_s = 0.0; st.success_until_time = 0.0;
			clicked = true;
		}
	}

	if (st.acquiring && na && na->CurrentConfig().points > 0) {
		const int total = na->CurrentConfig().points;
		const int done = std::clamp(na->current_index(), 0, total);
		ImVec2 r_min = ImGui::GetItemRectMin();
		ImVec2 r_max = ImGui::GetItemRectMax();
		ImGui::GetWindowDrawList()->AddRectFilled(
			r_min, ImVec2(r_min.x + (r_max.x - r_min.x) * ((double)done / (double)total), r_max.y),
			IM_COL32(80, 255, 100, 120), ImGui::GetStyle().FrameRounding);
	}

	return clicked;
}
inline void DrawExportRow2Col(const char* whichLabel,    // "OSC1", "Spectrum", etc.
	ExportRowState& state,
	const std::vector<double>& x,
	const std::vector<double>& y,
	const char* xHeader,       // e.g. "Time" or "Frequency"
	const char* yHeader,       // e.g. "Voltage" or "Magnitude"
	const char* fileExtension, // e.g. "csv"
	float comboWidth = 100.f,
	float buttonWidth = 100.f)
{
	const char* destList[] = { "clipboard", "csv" };

	// ----- Button label & status flash -----
	std::string btnLabel = std::string("Export ") + whichLabel;
	int frame = ImGui::GetFrameCount();

	if (state.copiedFlag) {
		state.copiedFrame = frame;
		state.copiedFlag = false;
	}
	if ((frame - state.copiedFrame) < state.copiedLingerFrames) {
		// Show different text depending on what we just did
		btnLabel = state.lastWasClipboard ? "Copied!" : "Saved!";
	}

	// ----- Export button -----
	if (WhiteOutlineButton((btnLabel + "##" + whichLabel + "ExportButton").c_str(),
		ImVec2(buttonWidth, 30)))
	{
		if (state.destComboIdx == 0) {
			// Clipboard
			if (Export2ColToClipboard(x, y, xHeader, yHeader)) {
				state.lastWasClipboard = true;
				state.copiedFlag = true;
			}
		}
		else {
			// CSV via async save dialog (was blocking NFD). Data vectors are
			// captured by value; 'state' outlives the dialog, capture by reference.
			ShowSaveFileDialog(fileExtension, [=, &state](const char* path) {
				if (path) {
					if (Export2ColToCsvFile(path, fileExtension, x, y, xHeader, yHeader)) {
						state.lastWasClipboard = false;
						state.copiedFlag = true;
					}
				}
			});
		}
	}

	// ----- "to [combo]" -----
	ImGui::SameLine();
	ImGui::Text("to");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(comboWidth);
	ImGui::Combo((std::string("##") + whichLabel + "ExportDestCombo").c_str(),
		&state.destComboIdx,
		destList,
		IM_ARRAYSIZE(destList));
}


#endif