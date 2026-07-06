#include "ControlWidget.hpp"
#include "librador.h"
#include "UIComponents.hpp"
#include "implot.h"
#include "util.h"
#include "SignalType.hpp"
#include "WaveformFile.h"

#include <string>
#include <utility>
#include <vector>

/// <summary>Signal Generator Widget
/// </summary>
class SGControl : public ControlWidget
{
public:

	// Constructor
	SGControl(std::string label, ImVec2 size, const float* accentColour, int channel,
	     float* getPSUVoltage)
	    : ControlWidget(label, size, accentColour)
	    , channel(channel)
	    , active(false)
	    , switched(false)
	    , label(label)
	    , signal_idx(0)
	    , pPSUVoltage(getPSUVoltage)
	{
		// The four procedural shapes, exactly as before (generated on the fly,
		// with preview + duty cycle for Square).
		signals.push_back(new SineSignal("Sine"));
		signals.push_back(new SquareSignal("Square"));
		signals.push_back(new SawtoothSignal("Sawtooth"));
		signals.push_back(new TriangleSignal("Triangle"));

		// Bundled .tlw arbitrary waveforms (Qt parity). Entries that duplicate
		// a procedural shape are skipped — the procedural version has phase
		// (and duty cycle) that the raw sample file cannot offer.
		for (const std::string& name : WaveformFile::availableWaveforms())
		{
			if (name == "Sin" || name == "Square" || name == "Sawtooth"
			    || name == "Triangle")
				continue;
			signals.push_back(new ArbitrarySignal(WaveformFile::load(name)));
		}
	}

	// Destructor
	~SGControl()
	{
		for (GenericSignal* signal : signals)
		{
			delete signal;
		}
	}
	/// <summary>
	/// Render UI elements for Signal Generator
	/// </summary>
	void renderControl() override
	{
		ImGui::Dummy(ImVec2(0.0f, 5.0f)); // add space
		ImGui::Text("Power");
		ImGui::SameLine();
		ImGui::Text("   OFF");
		ImGui::SameLine();
		switched
		    = ToggleSwitch((label + "_toggle").c_str(), &active, colourConvert(accentColour));
		ImGui::SameLine();
		ImGui::Text("ON");

		switched |= ObjectDropDown(("##" + label + "wt_selector").c_str(), signals.data(),
		    &signal_idx, static_cast<int>(signals.size()));

		ImGui::SeparatorText(
		    ((signals[signal_idx]->getLabel()) + " Wave Properties").c_str());

		// Make preview colour a little brighter
		// ImPlot fork: SetNextLineStyle removed; colour is passed to the preview's
		// PlotLine via ImPlotSpec inside GenericSignal (see setPreviewLineColor).
		signals[signal_idx]->setPreviewLineColor(ImVec4(accentColour[0]*1.4, accentColour[1]*1.4, accentColour[2]*1.4, 1.0f));
		switched = (signals[signal_idx]->renderControl()) || switched;

		// The signal generators must never drive the PSU rail up (per Chris,
		// 2026-07-04): the output peak is hard-clamped to what the current
		// PSU voltage supports (1.18 V amplifier headroom), and the warning
		// explains why the controls stopped moving.
		if (signals[signal_idx]->clampSignalMax(*pPSUVoltage - 1.18f))
		{
			switched = true; // resend the clamped settings
		}
		if (signals[signal_idx]->getSignalMax() >= *pPSUVoltage - 1.18f - 0.01f)
		{
			ImGui::SetTooltip("Signal generator output is limited by the Power Supply\n"
			                  "voltage. Increase the PSU voltage for more range.");
		}

		// UART TX is a CH1-only feature, as in the Qt app. The desktop layout
		// relocates it to the Logic page (ShowUartInline = false) and calls
		// renderUartControl there itself.
		if (channel == 1 && ShowUartInline)
		{
			renderUartControl();
		}
	}

	/// <summary>
	/// Set the Signal Generator on the labrador board.
	/// </summary>
	bool controlLab() override
	{
		// While UART TX owns CH1 the waveform controls stay editable but do
		// not drive the hardware (Qt backup_waveform/restore_waveform model).
		if (channel == 1 && (uart_enabled || uart_restore_pending))
		{
			return uartService();
		}

		// PSU-follows clamp also catches the PSU being lowered while this
		// widget is hidden (mobile tiles) — controlLab runs every frame.
		if (active && signals[signal_idx]->clampSignalMax(*pPSUVoltage - 1.18f))
		{
			switched = true;
		}

		// resend_pending forces a re-push after a USB reconnect: the board
		// was reset and has forgotten its generator state, but nothing in the
		// UI changed, so `switched` is false. It is a separate flag because
		// renderControl() overwrites `switched` every frame (and only runs
		// while the widget is visible), which would otherwise drop the resend.
		if (switched || resend_pending)
		{
			// A reconnect resend must survive a transient send failure (the
			// link can still be settling in the frames right after
			// onDeviceConnected): only clear the flag once the hardware
			// accepted the settings, so the next frame retries. `switched`
			// sends stay fire-and-forget — the user sees those and retries.
			const int error = active ? signals[signal_idx]->controlLab(channel)
			                         : signals[signal_idx]->turnOff(channel);
			if (error == 0)
			{
				resend_pending = false;
			}
			return true;
		}
		return false;

	}

	// Turn the generator output off on the hardware. Used on app shutdown, so
	// it drives the device directly rather than deferring to controlLab (which
	// won't run again).
	void reset()
	{
		uart_queue.clear();
		uart_transmitting = false;
		signals[signal_idx]->turnOff(channel);
	}

	// Force the next controlLab() to re-push this widget's current state to the
	// board — used after a USB reconnect, when the device has forgotten its
	// generator settings. Mirrors InputsControl::markDirty / DigitalOutControl.
	// Without this the signal generator silently stayed dark after every
	// reconnect until the user toggled a control.
	void markDirty()
	{
		uart_queue.clear();
		uart_transmitting = false;
		if (uart_enabled)
		{
			uart_idle_pending = true; // re-assert the idle-high line on reconnect
		}
		resend_pending = true;
	}

	// Where the UART TX section renders: inline under the waveform controls
	// (classic/lowres) or hosted by another page (desktop Logic panel).
	bool ShowUartInline = true;

	/// <summary>
	/// Collapsible UART TX section (CH1 only). Port of the Qt app's serial
	/// encoding path: text is framed as 8N1 logic-level UART (idle high) and
	/// sent as the CH1 waveform at one sample per bit. Public so the desktop
	/// Logic page can host it.
	/// </summary>
	void renderUartControl()
	{
		ImGui::Dummy(ImVec2(0.0f, 5.0f));
		if (!ImGui::CollapsingHeader(("UART TX (8N1)##" + label).c_str()))
		{
			return;
		}

		if (ImGui::Checkbox(("Drive CH1 as UART line##" + label).c_str(), &uart_enabled))
		{
			if (uart_enabled)
			{
				// Qt backup_waveform + idle: take over CH1, hold the line at
				// mark level until something is sent.
				uart_idle_pending = true;
			}
			else
			{
				// Qt restore_waveform: hand CH1 back to the waveform controls.
				uart_restore_pending = true;
			}
		}
		if (uart_enabled)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(wave settings above are suspended)");
		}

		ImGui::SetNextItemWidth(ScaledPx(100.0f));
		ImGui::Combo(("Baud##" + label).c_str(), &uart_baud_idx, uart_baud_labels,
		    IM_ARRAYSIZE(uart_baud_labels));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ScaledPx(100.0f));
		if (ImGui::DragFloat(("TX level##" + label).c_str(), &uart_level_v, 0.05f, 0.5f,
		        9.0f, "%.2f V", ImGuiSliderFlags_AlwaysClamp)
		    && uart_enabled)
		{
			uart_idle_pending = true; // re-drive the idle line at the new level
		}
		ImGui::Checkbox(("Append \\r\\n##" + label).c_str(), &uart_append_crlf);

		ImGui::BeginDisabled(!uart_enabled);
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
		bool send = ImGui::InputText(("##uart_tx_input" + label).c_str(), uart_text,
		    IM_ARRAYSIZE(uart_text), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		send |= ImGui::Button(("Send##" + label).c_str());
		ImGui::EndDisabled();

		if (send)
		{
			std::string payload(uart_text);
			if (uart_append_crlf)
			{
				payload += "\r\n";
			}
			if (!payload.empty())
			{
				for (std::vector<unsigned char>& burst : uartEncodeBursts(payload))
				{
					uart_queue.push_back(std::move(burst));
				}
			}
			uart_text[0] = '\0';
		}

		if (uart_transmitting || !uart_queue.empty())
		{
			ImGui::Text("Transmitting... %d burst(s) queued",
			    static_cast<int>(uart_queue.size()));
		}
		else if (uart_enabled)
		{
			ImGui::TextDisabled("Line idle high at %.2f V", uart_level_v);
		}
	}

private:
	/// <summary>
	/// Port of the Qt app's MainWindow::uartEncode bit framing, restricted to
	/// 8N1 (no parity — Qt's optional parity path has a known bug where it
	/// counts one-bits over the whole accumulated buffer):
	///   - 300-sample idle-high preamble (Qt's initialisation run),
	///   - per byte: start bit (0), 8 data bits LSB first (0/255), stop bit (255),
	///   - one sample per bit; playback at usecs_between_samples = 1e6/baud
	///     makes the on-wire rate exactly the baud rate (Qt sets
	///     freq = baud/length, i.e. timer period CLOCK_FREQ/(div*baud)).
	/// librador's per-channel buffer holds 512 samples and only has the
	/// repeat-forever command (0xa2; the Qt one-shot 0xb2 was never wired up
	/// in librador), so long text is split into <=21-character bursts, each
	/// padded to 512 samples with idle so a repeating buffer spends most of
	/// its cycle at mark level; uartService swaps in the next burst (or the
	/// idle line) after one full pass.
	/// </summary>
	static std::vector<std::vector<unsigned char>> uartEncodeBursts(
	    const std::string& text)
	{
		constexpr int IDLE_PREAMBLE = 300; // Qt uartEncode's leading mark run
		constexpr int FRAME_BITS = 10; // start + 8 data + stop
		constexpr int MAX_SAMPLES = WaveformFile::FGEN_BUFFER_SAMPLES;
		constexpr size_t CHARS_PER_BURST = (MAX_SAMPLES - IDLE_PREAMBLE) / FRAME_BITS;

		std::vector<std::vector<unsigned char>> bursts;
		for (size_t start = 0; start < text.size(); start += CHARS_PER_BURST)
		{
			std::vector<unsigned char> burst(IDLE_PREAMBLE, 255);
			const size_t stop = std::min(text.size(), start + CHARS_PER_BURST);
			for (size_t i = start; i < stop; ++i)
			{
				const unsigned char byte = static_cast<unsigned char>(text[i]);
				burst.push_back(0); // start bit (space)
				for (int bit = 0; bit < 8; ++bit) // data bits, LSB first
				{
					burst.push_back(((byte >> bit) & 1) ? 255 : 0);
				}
				burst.push_back(255); // stop bit (mark)
			}
			burst.resize(MAX_SAMPLES, 255); // trailing idle pad (see above)
			bursts.push_back(std::move(burst));
		}
		return bursts;
	}

	// Push a sample buffer to CH1 at one sample per bit. librador applies the
	// same amplitude/offset scaling and Xmega timer maths as the Qt driver.
	void uartSendBuffer(std::vector<unsigned char>& buf)
	{
		const double usecs_per_bit = 1e6 / static_cast<double>(uart_baud_rates[uart_baud_idx]);
		librador_update_signal_gen_settings(channel, buf.data(),
		    static_cast<int>(buf.size()), usecs_per_bit, uart_level_v, 0.0);
	}

	/// <summary>
	/// Per-frame UART state machine, run from controlLab while UART TX owns
	/// CH1 (so it keeps ticking even when the widget is hidden). Returns true
	/// if settings were sent to the device this frame.
	/// </summary>
	bool uartService()
	{
		if (uart_restore_pending)
		{
			uart_restore_pending = false;
			uart_queue.clear();
			uart_transmitting = false;
			if (active)
			{
				signals[signal_idx]->controlLab(channel);
			}
			else
			{
				signals[signal_idx]->turnOff(channel);
			}
			return true;
		}

		const double now = ImGui::GetTime();
		if (uart_transmitting)
		{
			if (now < uart_burst_end_time)
			{
				return false;
			}
			// First full pass of the burst has played out; replace it with
			// the next burst (or the idle line) during its idle padding.
			uart_transmitting = false;
			uart_idle_pending = true;
		}

		if (!uart_queue.empty())
		{
			std::vector<unsigned char> burst = std::move(uart_queue.front());
			uart_queue.erase(uart_queue.begin());
			uartSendBuffer(burst);
			const double bit_seconds
			    = 1.0 / static_cast<double>(uart_baud_rates[uart_baud_idx]);
			uart_burst_end_time = now + static_cast<double>(burst.size()) * bit_seconds;
			uart_transmitting = true;
			uart_idle_pending = false;
			return true;
		}

		if (uart_idle_pending)
		{
			uart_idle_pending = false;
			std::vector<unsigned char> idle(32, 255); // hold the line at mark
			uartSendBuffer(idle);
			return true;
		}
		return false;
	}

	const std::string label;
	int channel;
	bool active;
	bool switched;
	bool resend_pending = false; // re-push waveform on the next controlLab (reconnect)
	int signal_idx = 0;
	std::vector<GenericSignal*> signals;
	float* pPSUVoltage;

	// --- UART TX state (CH1 only) ---
	// Qt preset baud list (same as the UART decoder's)
	static constexpr int uart_baud_rates[12] = { 300, 600, 1200, 2400, 4800, 9600,
		14400, 19200, 28800, 38400, 57600, 115200 };
	static constexpr const char* uart_baud_labels[12] = { "300", "600", "1200", "2400",
		"4800", "9600", "14400", "19200", "28800", "38400", "57600", "115200" };
	bool uart_enabled = false;
	bool uart_idle_pending = false; // (re)assert the idle-high line
	bool uart_restore_pending = false; // hand CH1 back to the waveform controls
	bool uart_transmitting = false;
	double uart_burst_end_time = 0.0;
	int uart_baud_idx = 5; // 9600
	float uart_level_v = 3.3f; // logic-high voltage (samples are 0/255)
	bool uart_append_crlf = true; // Qt sends "\r\n" line endings
	char uart_text[256] = "";
	std::vector<std::vector<unsigned char>> uart_queue;
};
