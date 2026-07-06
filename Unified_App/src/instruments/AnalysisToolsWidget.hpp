#pragma once
#include "ControlWidget.hpp"
#include <chrono>
#include <array>
#include "UIComponents.hpp"
#include "AcquireState.hpp"


using Clock = std::chrono::steady_clock;

class AnalysisToolsWidget : public ControlWidget
{
public:
	

	struct {
		bool AcquisitionExists = false;
		int  SignalComboCurrentItem = 0;
		bool Acquire = false;
		int  SampleRatesComboCurrentItem = 0;
		int  WindowComboCurrentItem = 0;
		float TimeWindow = 1.f;
		float LookbackTimeWindow = 1.f;
		const char* SignalComboList[2] = { "OSC1", "OSC2" };

		std::array<const char*, 56> SampleRatesComboList{};
		std::array<std::string, 56> SampleRatesLabels{};
		std::array<int, 56> SampleRatesValues{};

		const char* WindowComboList[2] = { "Hann", "Rectangular" };
		float ItemWidth = 100.0f;
		const char* UnitsComboList[3] = { "dBm", "dBV", "V RMS" };
		int UnitsComboCurrentItem = 0;
		bool Autofit = false;
		const char* AcquisitionModeComboList[2] = { "Gated (Start-Stop)", "Lookback" };
		int AcquisitionModeComboCurrentItem = 0;
		int LastAcquiredMode = 0;
		SpectrumAcquireState AcquireState;
		double OSC1_last_captured = -1;
		double OSC2_last_captured = -1;
		bool DisplayOSC1 = true;
		bool DisplayOSC2 = true;
	} SA;

	

	struct {
		bool Acquire = false;
		const char* SignalGenComboList[2] = { "SG1", "SG2" };
		const char* OscComboList[2] = { "OSC1", "OSC2" };
		int  StimulusComboCurrentItem = 0;
		int  InputComboCurrentItem = 0;
		int  ResponseComboCurrentItem = 1;
		float ComboBoxWidth = 100.0f;
		bool PhaseOn = false;
		float ItemWidth = 100.0f;
		double f_start = 10;
		double f_stop = 1000;
		const char* UnitsComboList[2] = { "dB", "Linear" };
		int  UnitsComboCurrentItem = 0;
		bool Autofit = false;
		bool AcquisitionExists = false;
		int  PointSpacingComboCurrentItem = 0;
		const char* PointSpacingComboList[2] = { "Logarithmic", "Linear" };
	} NA;

	bool ToolsOn = false;
	int CurrentTab = 0;
	// Programmatic tab switch (desktop Tools menu): set to 0 (spectrum) or
	// 1 (network); consumed on the next render.
	int RequestedTab = -1;
	ImColor OSC1Colour = colourConvert(constants::OSC1_ACCENT);
	ImColor OSC2Colour = colourConvert(constants::OSC2_ACCENT);
	const float* GenColour = constants::GEN_ACCENT;
	
	// Export states
	ExportRowState SpecOSC1ExportState;
	ExportRowState SpecOSC2ExportState;
	ExportRowState NAMagExportState;
	ExportRowState NAPhaseExportState;
	const char* ExportFileExtension = "csv"; // or use your existing FileExtension

	AnalysisToolsWidget(std::string label, ImVec2 size, const float* borderColor)
		: ControlWidget(label, size, borderColor)
	{
		buildSampleRatesCombo();
	}
	/// <summary>
	/// Render UI elements for analysis tools
	/// </summary>
	void renderControl() override
	{
		ImGui::Text(" OFF");
		ImGui::SameLine();
		ToggleSwitch("##AnalysisToolsDisplay", &ToolsOn, ImU32(colourConvert(GenColour)));
		ImGui::SameLine();
		ImGui::Text("ON");

		if (ToolsOn) {
			ImGui::SameLine();
			ImGui::TextColored(constants::GRAY_TEXT, "  Active tab will be displayed:");
		}

		if (ImGui::BeginTabBar("MyTabBar"))
		{
			if (ImGui::BeginTabItem("Spectrum Analyser", nullptr,
			        RequestedTab == 0 ? ImGuiTabItemFlags_SetSelected : 0))
			{
				CurrentTab = 0;
				// --- Top buttons ---
				if (ImGui::BeginTable("SpectrumButtons", 2))
				{
					ImGui::TableNextColumn();
					switch (SA.AcquisitionModeComboCurrentItem) {
					case 0: // gated
						if (DrawAcquireButton(SA.AcquireState)) {
							SA.Acquire = true;
							SA.TimeWindow = std::clamp(SA.AcquireState.elapsed_last_s, 0.01, (double)SA.AcquireState.max_time_s);
						}
						else SA.Acquire = false;
						break;
					case 1: // lookback
						SA.Acquire = WhiteOutlineButton("Acquire", ImVec2(ScaledPx(100), ScaledPx(30)));
						if (SA.Acquire) SA.TimeWindow = SA.LookbackTimeWindow;
						break;
					}
					ImGui::TableNextColumn();
					SA.Autofit = WhiteOutlineButton("Auto Fit##SpectrumAnalyser", ImVec2(ScaledPx(100), ScaledPx(30)));
					ImGui::TableNextColumn();
					if (SA.AcquisitionExists) {
						ImGui::TextDisabled("OSC1 Last captured: %.2f s", SA.OSC1_last_captured);
						ImGui::TextDisabled("OSC2 Last captured: %.2f s", SA.OSC2_last_captured);
					}
					ImGui::EndTable();
				}
				// --- Top buttons ---
				
				// --- Display toggles ---
				ImGui::SeparatorText("Display");
				if (ImGui::BeginTable("SpectrumChannelsTable", 4))
				{
					const float width = ImGui::GetContentRegionAvail().x * 0.95f;
					float labWidth = ImGui::CalcTextSize("Channel 1 (OSC1)").x + 12.0f;
					float controlWidth = (width - 2 * labWidth) / 2;
					ImGui::TableSetupColumn("One", ImGuiTableColumnFlags_WidthFixed, labWidth);
					ImGui::TableSetupColumn("Two", ImGuiTableColumnFlags_WidthFixed, controlWidth);
					ImGui::TableSetupColumn("Three", ImGuiTableColumnFlags_WidthFixed, labWidth);
					ImGui::TableSetupColumn("Four", ImGuiTableColumnFlags_WidthFixed, controlWidth);

					ImGui::TableNextColumn(); ImGui::Text("Channel 1 (OSC1)");
					ImGui::TableNextColumn(); ToggleSwitch((label + "Display1_toggle").c_str(), &SA.DisplayOSC1, ImU32(OSC1Colour));

					ImGui::TableNextColumn(); ImGui::Text("Channel 2 (OSC2)");
					ImGui::TableNextColumn(); ToggleSwitch((label + "Display2_toggle").c_str(), &SA.DisplayOSC2, ImU32(OSC2Colour));

					

					ImGui::EndTable();
				}
				ImGui::SeparatorText("General");
				// Export
				std::string magLabel =
					std::string("Magnitude (") +
					SA.UnitsComboList[SA.UnitsComboCurrentItem] +
					")";
				// Get OSC1 data (upstream Monash bug read osc2 here)
				std::vector<double> f1 = spectrum_plots->osc1.x;
				std::vector<double> m1 = spectrum_plots->osc1.y;

				DrawExportRow2Col("OSC1 Spectrum",
					SpecOSC1ExportState,
					f1, m1,
					"Frequency (Hz)", magLabel.c_str(),
					ExportFileExtension);

				// Get OSC2 data
				std::vector<double> f2 = spectrum_plots->osc2.x;
				std::vector<double> m2 = spectrum_plots->osc2.y;

				DrawExportRow2Col("OSC2 Spectrum",
					SpecOSC2ExportState,
					f2, m2,
					"Frequency (Hz)", magLabel.c_str(),
					ExportFileExtension);
				// Advanced Options
				ImGui::SetNextItemOpen(false, ImGuiCond_Once);
				if (ImGui::CollapsingHeader("Advanced Options##SpectrumOptions", ImGuiTreeNodeFlags_SpanAvailWidth)) {
					if (ImGui::BeginTable("SpectrumAdvTbl", 2, ImGuiTableFlags_SizingStretchProp)) {
						ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 170.0f);
						ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);

						auto row = [&](const char* label, auto widget) {
							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label);
							ImGui::TableSetColumnIndex(1);
							ImGui::SetNextItemWidth(SA.ItemWidth > 0 ? SA.ItemWidth : -FLT_MIN);
							widget();
						};

						row("Sample Rate", [&] {
							ImGui::Combo("##SampleRate",
								&SA.SampleRatesComboCurrentItem,
								SA.SampleRatesComboList.data(),
								(int)SA.SampleRatesComboList.size());
						});

						if (SA.AcquisitionModeComboCurrentItem == 0) {
							row("Max Time Window", [&] {
								ImGui::SliderFloat("##MaxTimeWindow", &SA.AcquireState.max_time_s, 0.1f, 20.0f, "%.1f s");
							});
						}
						else {
							row("Time Window", [&] {
								ImGui::SliderFloat("##TimeWindow", &SA.LookbackTimeWindow, 0.1f, 20.0f, "%.1f s");
							});
						}

						row("Windowing Function", [&] {
							ImGui::Combo("##Windowing", &SA.WindowComboCurrentItem, SA.WindowComboList, IM_ARRAYSIZE(SA.WindowComboList));
						});

						row("Vertical Units", [&] {
							ImGui::Combo("##Vertical Units", &SA.UnitsComboCurrentItem, SA.UnitsComboList, IM_ARRAYSIZE(SA.UnitsComboList));
						});

						row("Acquisition Mode", [&] {
							ImGui::Combo("##AcquisitionType", &SA.AcquisitionModeComboCurrentItem,
								SA.AcquisitionModeComboList, IM_ARRAYSIZE(SA.AcquisitionModeComboList));
						});

						ImGui::EndTable();
					}
				}
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Network Analyser", nullptr,
			        RequestedTab == 1 ? ImGuiTabItemFlags_SetSelected : 0))
			{
				CurrentTab = 1;
				if (ImGui::BeginTable("NetworkButtons", 2))
				{
					ImGui::TableNextColumn();
					// acquire button with overlay/progress
					static NetworkAcquireState st;
					if (DrawNetworkAcquireButton(st, na_, cfg_, ImVec2(ScaledPx(200), ScaledPx(30)))) {
#ifndef NDEBUG
						printf("Network Analyser acquisition started\n");
#endif
					}
					ImGui::TableNextColumn();
					NA.Autofit = WhiteOutlineButton("Auto Fit##NetworkAnalyser", ImVec2(ScaledPx(100), ScaledPx(30)));
					ImGui::EndTable();
				}
				
				ImGui::SeparatorText("Display");

				ImGui::Text("Phase"); ImGui::SameLine();
				ToggleSwitch("##NetworkAnalyserPhaseOn", &NA.PhaseOn, ImU32(colourConvert(GenColour)));

				ImGui::SeparatorText("Options");
				
				ImGui::Text("Stimulus Generator"); ImGui::SameLine();
				ImGui::SetNextItemWidth(NA.ComboBoxWidth);
				ImGui::Combo("##NetworkAnalyserGenCombo", &NA.StimulusComboCurrentItem,
					NA.SignalGenComboList, IM_ARRAYSIZE(NA.SignalGenComboList));

				ImGui::Text("Reference (Input) Channel"); ImGui::SameLine();
				int& in = NA.InputComboCurrentItem;
				int& out = NA.ResponseComboCurrentItem;
				in = (in != 0); out = (out != 0);
				if (in == out) out = 1 - in;
				ImGui::SetNextItemWidth(NA.ComboBoxWidth);
				bool changed_in = ImGui::Combo("##NetworkAnalyserInputCombo", &in, NA.OscComboList, IM_ARRAYSIZE(NA.OscComboList));
				if (changed_in) out = 1 - in;

				ImGui::Text("Response (Output) Channel"); ImGui::SameLine();
				ImGui::SetNextItemWidth(NA.ComboBoxWidth);
				bool changed_out = ImGui::Combo("##NetworkAnalyserResponseCombo", &out, NA.OscComboList, IM_ARRAYSIZE(NA.OscComboList));
				if (changed_out) in = 1 - out;

				// push into cfg if present
				if (cfg_) {
					cfg_->gen_channel = NA.StimulusComboCurrentItem + 1; // SG1=1, SG2=2
					cfg_->ch_input = (in == 0 ? 1 : 2);
					cfg_->ch_output = (out == 0 ? 1 : 2);
				}

				RangeSliderDoubleLog("Frequency Range", &NA.f_start, &NA.f_stop,
					1, 5000, 0.1, true, true, 0.0f, 5.0f, 8.0f);
				if (cfg_) { cfg_->f_start = NA.f_start; cfg_->f_stop = NA.f_stop; cfg_->use_ifbw_limits = false; }

				ImGui::SeparatorText("General");
				// Export
				std::string magLabel =
					std::string("Magnitude (") +
					NA.UnitsComboList[NA.UnitsComboCurrentItem] +
					")";
				// Get magnitude data
				std::vector<double> f = network_plots->freq;
				std::vector<double> m = network_plots->mag;

				DrawExportRow2Col("Magnitude##FR",
					NAMagExportState,
					f, m,
					"Frequency (Hz)", magLabel.c_str(),
					ExportFileExtension);

				// Get phase data
				std::vector<double> p = network_plots->phase;

				DrawExportRow2Col("Phase##FR",
					NAPhaseExportState,
					f, p,
					"Frequency (Hz)", "Phase (rads)",
					ExportFileExtension);

				ImGui::SetNextItemOpen(false, ImGuiCond_Once);
				if (ImGui::CollapsingHeader("Advanced Options##NetworkOptions", ImGuiTreeNodeFlags_SpanAvailWidth)) {
					if (ImGui::BeginTable("NetworkAdvTbl", 2, ImGuiTableFlags_SizingStretchProp)) {
						ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 170.0f);
						ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);

						auto row = [&](const char* label, auto widget) {
							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label);
							ImGui::TableSetColumnIndex(1);
							ImGui::SetNextItemWidth(NA.ItemWidth > 0 ? NA.ItemWidth : -FLT_MIN);
							widget();
						};

						row("Vertical Units", [&] {
							ImGui::Combo("##Vertical Units", &NA.UnitsComboCurrentItem, NA.UnitsComboList, IM_ARRAYSIZE(NA.UnitsComboList));
						});

						row("Number of Data Points", [&] {
							if (cfg_) ImGui::SliderInt("##Points", &cfg_->points, 2, 501, "%d",
								ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat);
							else { int dummy = 201; ImGui::SliderInt("##Points", &dummy, 2, 501); }
						});

						row("Point Spacing", [&] {
							ImGui::Combo("##PointSpacing", &NA.PointSpacingComboCurrentItem, NA.PointSpacingComboList, IM_ARRAYSIZE(NA.PointSpacingComboList));
						});
						if (cfg_) cfg_->log_spacing = (NA.PointSpacingComboCurrentItem == 0);

						ImGui::EndTable();
					}
				}
				ImGui::EndTabItem();
			}
		}
		ImGui::EndTabBar();
		RequestedTab = -1;
	}

	bool controlLab() override
	{
		return false;
	}
	void SetNetworkAnalyser(NetworkAnalyser* na, NetworkAnalyser::Config* cfg) {
		na_ = na; cfg_ = cfg;
	}
private:
	NetworkAnalyser* na_ = nullptr;
	NetworkAnalyser::Config* cfg_ = nullptr;
	void buildSampleRatesCombo() {
		auto fmt_hz_khz = [](int hz)->std::string {
			if (hz >= 1000) {
				if (hz % 1000 == 0) return std::to_string(hz / 1000) + " kHz";
				char buf[32]; std::snprintf(buf, sizeof(buf), "%.1f kHz", hz / 1000.0); return std::string(buf);
			}
			else return std::to_string(hz) + " Hz";
		};
		for (int i = 0; i < 56; ++i) {
			SA.SampleRatesLabels[i] = fmt_hz_khz(constants::DIVISORS_375000[55 - i]);
			SA.SampleRatesComboList[i] = SA.SampleRatesLabels[i].c_str();
			SA.SampleRatesValues[i] = constants::DIVISORS_375000[55 - i];
		}
	}
};
