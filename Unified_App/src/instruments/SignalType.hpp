#include "librador.h"
#include "implot.h"
#include "util.h"
#include "UIComponents.hpp"
#include "WaveformFile.h"
#include <algorithm>
#include <string>
#include <cmath>
#include <vector>
#include <iomanip>
#include <sstream>

/// <summary>
/// Abstract class representing signal from signal generator
/// </summary>
class GenericSignal
{
public:
	GenericSignal(const std::string& label, float* preview)
	    : GenericSignal(label, preview, 100.f, 1.0f, 1e6f)
	{
	}

	// Overload used by ArbitrarySignal: .tlw files carry per-file frequency
	// limits (see WaveformFile). The procedural shapes keep the historical
	// 1 Hz - 1 MHz range via the delegating constructor above.
	GenericSignal(const std::string& label, float* preview, float freq_default,
	    float freq_min, float freq_max)
	    : label(label)
	    , preview(preview)
	    , amplitude("##" + label + "amp", "Vpeak-peak", 1.0f, 0.15f, 9.0f, "V",
	          constants::volt_prefs, constants::volt_formats)
	    , frequency("##" + label + "freq", "Frequency", freq_default, freq_min, freq_max,
	          "Hz", constants::freq_prefs, constants::freq_formats)
	    , offset("##" + label + "os", "Vbase", 0.0f, 0.0f, 9.0f, "V",
	          constants::volt_prefs, constants::volt_formats)
		, phase("##" + label + "phase", "Phase", 0.0f, 0.0f, 360.0f, "deg", constants::phase_prefs, constants::phase_formats)
	{
	}

	virtual ~GenericSignal() = default;

	std::string getLabel() const
	{
		return label;
	}

	// ImPlot fork: SetNextLineStyle removed; the caller (SGControl) provides the
	// preview line colour here and it is applied via ImPlotSpec in renderPreview.
	void setPreviewLineColor(const ImVec4& col)
	{
		previewLineColor = col;
	}

	/// <summary>
	/// Generic UI elements for Signal Control
	/// </summary>
	bool renderControl()
	{
		const float width = ImGui::GetContentRegionAvail().x;
		const float height = ImGui::GetFrameHeightWithSpacing() * (label=="Square" ? 5.0f : 4.0f);

		// Controls
		ImGui::BeginChild((label + "_control").c_str(), ImVec2(width * 0.6, height));

		bool changed = renderProperties();

		ImGui::EndChild();

		// Preview
		ImGui::SameLine();
		ImPlotStyle backup = ImPlot::GetStyle();
		PreviewStyle();
		if (ImPlot::BeginPlot((label + "_preview").c_str(), ImVec2(width * 0.35, height),
		        ImPlotFlags_CanvasOnly | ImPlotFlags_NoInputs))
		{
			renderPreview();
		}
		// ImPlot::GetStyle() = backup;
		ImPlotContext& gp = *GImPlot;
		gp.Style = backup;
		return changed;
	}

	/// <summary>
	/// Render preview of signal
	/// </summary>
	void renderPreview()
	{
		// ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
		ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_None, ImPlotAxisFlags_None);

		int period = constants::PREVIEW_RES;
		int padding = period / 4;
		float plt_amp = 1.0f;

		// NEW
		int resolution = constants::PREVIEW_RES;
		float T = 1/frequency.getValue();

		float preview_x_range = preview_x_max - preview_x_min;
		if (preview_x_range < 2*T) {
			preview_x_min = -T;
			preview_x_max = 2.1*T;
		}
		else if (preview_x_max > 3.2*T) {
			preview_x_min = -T*0.5;
			preview_x_max = 1.6*T;
		}

		ImPlot::SetupAxisFormat(ImAxis_X1, T < 0.1? "%.2e" : "%.2f");

		float x_min = -T;
		float x_max = 2*T;

		float decade_min = -std::pow(10.0f, std::ceil(std::log10(-x_min)));
		float decade_max = std::pow(10.0f, std::ceil(std::log10(x_max)));

		auto xs = linspace(preview_x_min, preview_x_max, resolution);
		auto waveform = this->preview_generator(xs);

		float vbase = offset.getValue();
		float A = amplitude.getValue();
		float y_min = floor(vbase-0.5);
		float y_max = ceil(vbase + A + 0.5);
		std::string x_label = formatAxisLabel(T);
		const double x_ticks[] = {0, T};
		const char* x_labels[] = {"0", x_label.c_str()};

		std::string y_label_base = formatAxisLabel(vbase);
		std::string y_label_peak = formatAxisLabel(vbase+A);
		const double y_ticks[] = {vbase, vbase+A};
		const char* y_labels[] = {y_label_base.c_str(), y_label_peak.c_str()};
		float key_points_x[3] = {T, 0, 0};
		float key_points_y[3] = {0, vbase, vbase+A};

		// ImPlot::SetupAxesLimits(-5 - padding, period + padding + 5, 1.2, -1.2, ImGuiCond_Always);

		ImPlot::SetupAxisTicks(ImAxis_X1, x_ticks, 2, x_labels);
		ImPlot::SetupAxisTicks(ImAxis_Y1, y_ticks, 2, y_labels);
		ImPlot::SetupAxesLimits(preview_x_min, preview_x_max, y_min, y_max, ImPlotCond_Always);
		// ImPlot fork: line colour passed via ImPlotSpec (was SetNextLineStyle in SGControl)
		ImPlot::PlotLine(("##" + label + "_plot_preview").c_str(), xs.data(), waveform.data(), xs.size(),
		    ImPlotSpec(ImPlotProp_LineWeight, 2.5f, ImPlotProp_LineColor, previewLineColor));

		ImPlot::Annotation(preview_x_min, y_max, ImVec4(0, 0, 0, 0), ImVec2(0, 0), true, "V");
		ImPlot::Annotation(preview_x_max, y_min, ImVec4(0, 0, 0, 0), ImVec2(0, 0), true, "t (s)");

		// ImPlot::PlotScatter(("##" + label + "_period_pnt").c_str(), key_points_x, key_points_y, 3);
		// Plot half a waveform before and after preview
		// ImPlot::PlotLine(("##" + label + "_plot_preview_pre").c_str(),&preview[period-padding], padding+1, 1.0, (double) - padding);
		// ImPlot::PlotLine(("##" + label + "_plot_preview").c_str(), constants::x_preview,
		//     preview, period + 1);
		// ImPlot::PlotLine(("##" + label + "_plot_preview_post").c_str(),
		// 	preview, padding, 1.0, period);



		// Render annotations
		// float amp_label_x[2] = { 0, 0 };
		// float amp_label_y[2] = { plt_amp, -plt_amp };

		// ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1, 1, 1, 1));
		// ImPlot::PlotLine(("##" + label + "_amp").c_str(), amp_label_x, amp_label_y, 2);
		// ImPlot::PlotScatter(("##" + label + "_amp_pnt").c_str(), amp_label_x, amp_label_y, 2);
		// // Annotate amplitude
		// ImPlot::Annotation(period*0.5, 0, ImVec4(0, 0, 0, 0), ImVec2(0, -5), true,
		// 	"Vpp = %.2f V", amplitude.getValue());

		// float per_label_x[2] = { 0, (float) period };
		// float per_label_y[2] = { 0.0f, 0.0f };

		// ImPlot::PlotLine(("##" + label + "_per").c_str(), per_label_x, per_label_y, 2);
		// ImPlot::PlotScatter(("##" + label + "_per_pnt").c_str(), per_label_x, per_label_y, 2);
		// // Annotate frequency
		// ImPlot::Annotation(period*0.5, 0, ImVec4(0, 0, 0, 0), ImVec2(0, 5), true,
		// 	"T = %.2E s", 1 / frequency.getValue());
		// ImPlot::PopStyleColor();

		ImPlot::EndPlot();
	}

	// Width of the property-table label column. Sized from the widest label at
	// the current font scale — a hardcoded 80 px clipped "Vpeak-peak" (and the
	// Square "Duty Cycle") once the text size was raised (font_scale 1.45).
	static float propertyLabelWidth()
	{
		float w = 0.0f;
		for (const char* lbl : { "Vpeak-peak", "Frequency", "Vbase", "Phase",
			     "Duty Cycle" })
			w = std::max(w, ImGui::CalcTextSize(lbl).x);
		return w + ImGui::GetStyle().ItemSpacing.x;
	}

	/// <summary>
	/// Render Control Widgets
	/// </summary>
	/// <returns></returns>
	virtual bool renderProperties()
	{
		bool changed = false;

		// Use the full child width: labels + value + unit are tight at large
		// font scales, so don't discard 10% of the column to a margin.
		const float width = ImGui::GetContentRegionAvail().x;
		const float labWidth = propertyLabelWidth();
		const float unitWidth = 50.0f;
		const float inpWidth = std::max(width - labWidth - unitWidth, 40.0f);

		if (ImGui::BeginTable((label + "_prop_table").c_str(), 2))
		{
			ImGui::TableSetupColumn("One", ImGuiTableColumnFlags_WidthFixed, labWidth);
			ImGui::TableSetupColumn("Two", ImGuiTableColumnFlags_WidthFixed, inpWidth + unitWidth);

			changed |= amplitude.renderInTable(inpWidth);
			changed |= frequency.renderInTable(inpWidth);
			changed |= offset.renderInTable(inpWidth);
			changed |= phase.renderInTable(inpWidth);

			if (label == "Square")
			{
				ImGui::TableNextColumn();
				ImGui::Text("Duty Cycle");
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(inpWidth);
				if (ImGui::DragInt(("##dc_control_" + label).c_str(), &dutycycle, 1, 1,
					100, "%d", ImGuiSliderFlags_AlwaysClamp))
				{
					changed = true;
					int period = constants::PREVIEW_RES;
					for (int i = 0; i < constants::PREVIEW_RES; i++)
					{
						preview[i] = (float)i / period < dutycycle * 0.01 ? 1.0 : -1.0;
					}
					preview[period] = preview[0];
				}
				ImGui::SameLine();
				ImGui::Text("%%");

			}

			ImGui::EndTable();
		}
		return changed;
	}

	/// <summary>
	/// Set the Signal Generator on the labrador board. Returns the librador
	/// error code (0 = accepted) so a reconnect resend can retry on a
	/// transient failure instead of silently dropping the state.
	/// </summary>
	virtual int controlLab(int channel) = 0;

	/// <summary>
	/// Set signal generator amplitude to zero. Returns the librador error
	/// code (0 = accepted), like controlLab.
	/// </summary>
	/// <param name="channel"></param>
	virtual int turnOff(int channel)
	{
		return librador_send_sin_wave(channel, 100, 0.0, 0.0);
	}

	virtual std::vector<float> preview_generator(std::vector<float> t) = 0;

	float getSignalMax()
	{
		return amplitude.getValue() + offset.getValue();
	}

	/// <summary>
	/// Hard-limit the output peak (amplitude + offset) to vmax by reducing
	/// amplitude first, then offset. Returns true if anything was clamped.
	/// </summary>
	bool clampSignalMax(float vmax)
	{
		if (vmax < 0.0f)
			vmax = 0.0f;
		float peak = amplitude.getValue() + offset.getValue();
		if (peak <= vmax)
			return false;
		float excess = peak - vmax;
		float amp_cut = std::min(excess, amplitude.getValue());
		amplitude.setLevel(amplitude.getValue() - amp_cut);
		excess -= amp_cut;
		if (excess > 0.0f)
			offset.setLevel(std::max(0.0f, offset.getValue() - excess));
		return true;
	}

private:
	float preview_x_min = -0.01;
	float preview_x_max = 0.05;
	float preview_y_min = -0.01;
	float preview_y_max = 1.0;
	std::string formatAxisLabel(float value) {
		std::ostringstream ss;

		// Use scientific notation if absolute value is less than 0.01 and not zero
		if (std::abs(value) < 0.0099 && value != 0.0f) {
			ss << std::scientific << std::setprecision(2) << value;
		} else {
			ss << std::fixed << std::setprecision(2) << value;
		}
		return ss.str();
	}

protected:
	std::string label;
	float* preview;
	SIValue amplitude;
	SIValue frequency;
	SIValue offset;
	SIValue phase;
	int dutycycle = 50;
	ImVec4 previewLineColor = IMPLOT_AUTO_COL; // ImPlot fork: replaces SetNextLineStyle
};


/// <summary>
/// Sine Signal Generator Widget
/// </summary>
class SineSignal : public GenericSignal
{
public:

	SineSignal(const std::string& label)
	    : GenericSignal(label, constants::sine_preview)
	{}

	/// <summary>
	/// Set the Signal Generator on the labrador board.
	/// </summary>
	int controlLab(int channel) override
	{
		return librador_send_sin_wave(channel, frequency.getValue(), amplitude.getValue(), offset.getValue(), phase.getValue()/180*M_PI);
	}

	std::vector<float> preview_generator(std::vector<float> t) override {
		std::vector<float> y(t.size());

		std::transform(t.begin(), t.end(), y.begin(), [this](float val) { return
			amplitude.getValue()/2*(1.0+std::sin(2*M_PI*frequency.getValue()*val-phase.getValue()/180*M_PI))+offset.getValue(); });
		return y;
	}

};

/// <summary>
/// Square Signal Generator Widget
/// </summary>
class SquareSignal : public GenericSignal
{
public:
	SquareSignal(const std::string& label)
	    : GenericSignal(label, constants::square_preview)
	{
	}

	/// <summary>
	/// Set the Signal Generator on the labrador board.
	/// </summary>
	int controlLab(int channel) override
	{
		return librador_imgui_send_square_wave(
		    channel, frequency.getValue(), amplitude.getValue(), offset.getValue(), dutycycle / 100.0, phase.getValue());
	}


	// Custom square function to adjust Duty Cycle. Could be integrated with future version of librador
	int librador_imgui_send_square_wave(int channel, double frequency_Hz, double amplitude_v, double offset_v, double duty_cycle = 0.5, double phase=0.0)
	{
		if ((amplitude_v + offset_v) > 9.6)
		{
			return -1;
			// Voltage range too high
		}
		if ((amplitude_v < 0) || (offset_v < 0))
		{
			return -2;
			// Negative voltage
		}

		if ((channel != 1) && (channel != 2))
		{
			return -3;
			// Invalid channel
		}
		int num_samples = fmin(1000000.0 / frequency_Hz, 512);
		// The maximum number of samples that Labrador's buffer holds is 512.
		// The minimum time between samples is 1us.  Using T=1/f, this gives a maximum
		// sample number of 10^6/f.
		num_samples = 2 * (num_samples / 2);
		// Square waves need an even number.  Others don't care.
		double usecs_between_samples = 1000000.0 / ((double)num_samples * frequency_Hz);
		// Again, from T=1/f.
		unsigned char* sampleBuffer = (unsigned char*)malloc(num_samples);

		int i;
		double x_temp;
		for (i = 0; i < num_samples; i++)
		{
			x_temp = (double)i * (2.0 * M_PI / (double)num_samples);
			// Generate points at interval 2*pi/num_samples.
			sampleBuffer[i] = sample_generator(x_temp-phase/180*M_PI, duty_cycle);
		}

		int error = librador_update_signal_gen_settings(channel, sampleBuffer, num_samples,
			usecs_between_samples, amplitude_v, offset_v);

		free(sampleBuffer);
		return error;
	}

	unsigned char sample_generator(double x, double duty_cycle = 0.5)
	{
		return (fmod(x+2*M_PI, 2*M_PI) < 2*M_PI*duty_cycle) ? 255 : 0;
	}

	std::vector<float> preview_generator(std::vector<float> t) override {
		std::vector<float> y(t.size());

		std::transform(t.begin(), t.end(), y.begin(), [this](float val) {
			// Square wave function with duty cycle
			float t = std::fmod(frequency.getValue() * val - phase.getValue() / 360.0f, 1.0f);
			if (t < 0) t += 1.0f; // Handle negative values
			return amplitude.getValue() * (t < dutycycle/100.0) + offset.getValue();
		});
		return y;
	}
};

/// <summary>
/// Sawtooth Signal Generator Widget
/// </summary>
class SawtoothSignal : public GenericSignal
{
public:
	SawtoothSignal(const std::string& label)
	    : GenericSignal(label, constants::sawtooth_preview)
	{
	}

	/// <summary>
	/// Set the Signal Generator on the labrador board.
	/// </summary>
	int controlLab(int channel) override
	{
		return librador_send_sawtooth_wave(
		    channel, frequency.getValue(), amplitude.getValue(), offset.getValue(), phase.getValue()/180*M_PI);
	}

	std::vector<float> preview_generator(std::vector<float> t) override {
		std::vector<float> y(t.size());

		std::transform(t.begin(), t.end(), y.begin(), [this](float val) {
			// Sawtooth wave function
			float t = std::fmod(frequency.getValue() * val - phase.getValue() / 360.0f, 1.0f);
			if (t < 0) t += 1.0f; // Handle negative values
			return amplitude.getValue() * t + offset.getValue();
		});
		return y;
	}
};

/// <summary>Triangle Signal Generator Widget
/// </summary>
class TriangleSignal : public GenericSignal
{
public:
	TriangleSignal(const std::string& label)
	    : GenericSignal(label, constants::triangle_preview)
	{
	}

	/// <summary>
	/// Set the Signal Generator on the labrador board.
	/// </summary>
	int controlLab(int channel) override
	{
		return librador_send_triangle_wave(channel, frequency.getValue(), amplitude.getValue(), offset.getValue(), phase.getValue()/180*M_PI);
	}

	std::vector<float> preview_generator(std::vector<float> t) override {
		std::vector<float> y(t.size());

		std::transform(t.begin(), t.end(), y.begin(), [this](float val) {
			// Triangle wave function
			float t = std::fmod(frequency.getValue() * val - phase.getValue() / 360.0f, 1.0f);
			if (t < 0) t += 1.0f; // Handle negative values
			return 2 * amplitude.getValue() * (0.5-std::abs(t - 0.5f)) + offset.getValue();
		});
		return y;
	}
};

/// <summary>Arbitrary waveform loaded from a bundled .tlw file (Qt parity:
/// adds DC and PRBS5, plus any user waveform listed in _list.wfl). The raw
/// samples are sent to the hardware with the frequency/amplitude/offset maths
/// of the Qt driver (functiongencontrol.cpp + genericusbdriver.cpp), and the
/// preview plots the actual file samples.
/// </summary>
class ArbitrarySignal : public GenericSignal
{
public:
	ArbitrarySignal(WaveformFile wf)
	    : GenericSignal(wf.name, nullptr,
	          // Qt defaults ChannelData::freq to 1 kHz; clamp into the file's
	          // playable range (see WaveformFile for the limit derivation).
	          std::clamp(1000.0f, static_cast<float>(wf.minFreq()),
	              static_cast<float>(wf.maxFreq())),
	          static_cast<float>(wf.minFreq()), static_cast<float>(wf.maxFreq()))
	    , file(std::move(wf))
	{
	}

	/// <summary>
	/// Set the Signal Generator on the labrador board.
	/// </summary>
	int controlLab(int channel) override
	{
		// Work on a copy: the T-stretch below must never mutate the file data.
		std::vector<unsigned char> buf(file.samples.begin(), file.samples.end());
		const double freq = frequency.getValue();

		// Qt genericUsbDriver::sendFunctionGenData T-stretching: halve the
		// sample count (keeping every 2^shift-th sample) until the playback
		// rate fits under the 1 MSPS DAC ceiling. The frequency control is
		// clamped to maxFreq, so shift never exceeds divisibility-1.
		int shift = 0;
		while (static_cast<double>(buf.size() >> shift) * freq > WaveformFile::DAC_SPS)
			shift++;
		if (shift != 0)
		{
			const size_t newLength = buf.size() >> shift;
			for (size_t i = 0; i < newLength; ++i)
				buf[i] = buf[i << shift];
			buf.resize(newLength);
		}

		// One waveform period = numSamples * usecs_between_samples = 1/freq.
		// librador converts this into the exact Xmega timer period + clock
		// divider pair the Qt driver computes (CLOCK_FREQ/(div*len*freq)-0.5).
		const double usecs_between_samples
		    = 1e6 / (static_cast<double>(buf.size()) * freq);
		return librador_update_signal_gen_settings(channel, buf.data(),
		    static_cast<int>(buf.size()), usecs_between_samples, amplitude.getValue(),
		    offset.getValue());
	}

	/// <summary>
	/// Amplitude/frequency/offset only: .tlw waveforms have no phase or duty
	/// cycle (same as the Qt app).
	/// </summary>
	bool renderProperties() override
	{
		bool changed = false;

		// Use the full child width: labels + value + unit are tight at large
		// font scales, so don't discard 10% of the column to a margin.
		const float width = ImGui::GetContentRegionAvail().x;
		const float labWidth = propertyLabelWidth();
		const float unitWidth = 50.0f;
		const float inpWidth = std::max(width - labWidth - unitWidth, 40.0f);

		if (ImGui::BeginTable((label + "_prop_table").c_str(), 2))
		{
			ImGui::TableSetupColumn("One", ImGuiTableColumnFlags_WidthFixed, labWidth);
			ImGui::TableSetupColumn("Two", ImGuiTableColumnFlags_WidthFixed, inpWidth + unitWidth);

			changed |= amplitude.renderInTable(inpWidth);
			changed |= frequency.renderInTable(inpWidth);
			changed |= offset.renderInTable(inpWidth);

			ImGui::EndTable();
		}
		return changed;
	}

	// Preview shows the actual file samples (sample-and-hold between points).
	std::vector<float> preview_generator(std::vector<float> t) override
	{
		std::vector<float> y(t.size());
		const int n = static_cast<int>(file.samples.size());

		std::transform(t.begin(), t.end(), y.begin(), [this, n](float val) {
			float u = std::fmod(frequency.getValue() * val, 1.0f);
			if (u < 0) u += 1.0f; // Handle negative values
			const int idx = std::min(static_cast<int>(u * n), n - 1);
			return amplitude.getValue() * (file.samples[idx] / 255.0f) + offset.getValue();
		});
		return y;
	}

private:
	WaveformFile file;
};
