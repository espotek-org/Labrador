#pragma once
#include "ControlWidget.hpp"
#include "InputsControl.hpp"
#include "UIComponents.hpp"
#include "librador.h"
#include "util.h"
#include <algorithm>
#include <cstring>
#include <string>

/// <summary>Logic analyzer / protocol decode widget.
/// Design base: Brent's logic_decode_ui (Android_App). Shows a decode console
/// for each Inputs channel that is in logic mode. UART decoding runs
/// per-channel inside librador (uartStyleDecoder); I2C decoding uses both
/// channels at once (SDA = CH1, SCL = CH2 — see librador's i2cdecoder).
/// Decoding is DRIVEN by polling: librador only runs UartDecode /
/// i2cDecoder::run inside getMany_singleBit, so controlLab fetches digital
/// data every frame while decoding is active (in Brent's app the plot's
/// librador_get_digital_data calls provide this).
/// </summary>
class LogicDecodeControl : public ControlWidget
{
public:
	// Freeze the decode consoles: while set, controlLab stops polling the
	// digital stream (so librador's decoders stop consuming) and no new text
	// is appended. Settings changes still apply immediately on resume.
	bool Paused = false;

	LogicDecodeControl(std::string label, ImVec2 size, const float* accentColour)
	    : ControlWidget(label, size, accentColour)
	{}

	void SetInputs(InputsControl* inputs)
	{
		this->inputs = inputs;
	}

	// Force decode settings to be resent on the next controlLab (used after
	// reconnect, mirrors InputsControl::markDirty)
	void markDirty()
	{
		for (ChannelDecode& state : ch_state)
		{
			state.applied_baud_idx = -1;
		}
		applied_i2c_decoding = -1;
	}

	/// <summary>
	/// Render UI elements for logic decoding
	/// </summary>
	void renderControl() override
	{
		ImGui::Dummy(ImVec2(0.0f, 5.0f)); // add space

		bool logic[2] = { inputs != nullptr && inputs->channelIsLogic(1),
			inputs != nullptr && inputs->channelIsLogic(2) };
		if (!logic[0] && !logic[1])
		{
			// Not active until the Inputs widget puts a channel in logic mode
			ImGui::TextWrapped("Set an Inputs channel to logic to decode UART "
			                   "or I2C traffic.");
			return;
		}

		// Pause: freeze the consoles without touching the device mode
		ImGui::Text("Pause");
		ImGui::SameLine();
		ToggleSwitch((getLabel() + "_pause").c_str(), &Paused,
		    colourConvert(constants::GEN_ACCENT));
		if (Paused)
		{
			ImGui::SameLine();
			ImGui::TextColored(constants::GRAY_TEXT, "decode frozen");
		}

		// Protocol selection. I2C decodes both channels at once, so it needs
		// both of them in logic mode (SDA = CH1, SCL = CH2)
		ImGui::Text("Protocol");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5);
		if (ImGui::BeginCombo(
		        "##ld_protocol", protocol_sel == Protocol::I2C ? "I2C" : "UART"))
		{
			if (ImGui::Selectable("UART", protocol_sel == Protocol::UART))
			{
				protocol_sel = Protocol::UART;
			}
			bool i2c_allowed = logic[0] && logic[1];
			ImGui::BeginDisabled(!i2c_allowed);
			if (ImGui::Selectable("I2C", protocol_sel == Protocol::I2C))
			{
				protocol_sel = Protocol::I2C;
				// I2C owns both channels — UART decode cannot run alongside it
				// (Brent's i2c_allowed condition)
				ch_state[0].decode_on = false;
				ch_state[1].decode_on = false;
			}
			ImGui::EndDisabled();
			if (!i2c_allowed && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("I2C decoding needs both channels in logic mode.");
			}
			ImGui::EndCombo();
		}

		if (protocol_sel == Protocol::UART)
		{
			for (int ch = 1; ch <= 2; ch++)
			{
				if (logic[ch - 1])
				{
					renderUartChannel(ch);
				}
			}
		}
		else
		{
			renderI2c();
		}
	}

	/// <summary>
	/// Push decode settings to librador and poll the digital sample stream so
	/// the decoders run, then collect their output into the consoles.
	/// </summary>
	bool controlLab() override
	{
		if (inputs == nullptr)
		{
			return true;
		}
		if (!librador_is_connected())
		{
			// Board not connected (continue to run); settings stay pending and
			// are pushed once the board is back
			return false;
		}
		bool logic[2] = { inputs->channelIsLogic(1), inputs->channelIsLogic(2) };

		// Mode arbitration (mirrors Brent's logicDecodeUI::update()): drop any
		// decode selection the current device mode can no longer support
		if (protocol_sel == Protocol::I2C && !(logic[0] && logic[1]))
		{
			protocol_sel = Protocol::UART;
		}
		for (int ch = 1; ch <= 2; ch++)
		{
			ChannelDecode& state = ch_state[ch - 1];
			if (state.decode_on && (!logic[ch - 1] || protocol_sel != Protocol::UART))
			{
				state.decode_on = false;
			}
			// Push UART settings whenever they differ from what librador holds
			if (state.decode_on != state.applied_decode_on
			    || state.baud_idx != state.applied_baud_idx
			    || state.parity_idx != state.applied_parity_idx
			    || state.char_mode_idx != state.applied_char_mode_idx)
			{
				UartSettings settings;
				settings.decode_on = state.decode_on;
				settings.baudRate = static_cast<double>(baud_rates[state.baud_idx]);
				settings.parity = parities[state.parity_idx];
				settings.mode = char_modes[state.char_mode_idx];
				librador_set_uart_decode_settings(ch, settings);
				state.applied_decode_on = state.decode_on;
				state.applied_baud_idx = state.baud_idx;
				state.applied_parity_idx = state.parity_idx;
				state.applied_char_mode_idx = state.char_mode_idx;
			}
		}
		int want_i2c = (protocol_sel == Protocol::I2C) ? 1 : 0;
		if (want_i2c != applied_i2c_decoding)
		{
			librador_set_i2c_is_decoding(want_i2c == 1);
			applied_i2c_decoding = want_i2c;
		}

		// Paused: settings above still apply, but stop driving the decoders
		// (no polling = librador consumes no bits) and collecting output.
		if (Paused)
		{
			return true;
		}

		// Poll the digital sample stream to drive the decoders. The window /
		// sample count mirror Brent's plot poll (initial 0.5 s span,
		// GRAPH_SAMPLES = 512, delay 0); the values only shape the returned
		// plot vector (discarded here) — UartDecode always consumes every new
		// bit regardless
		bool ok = true;
		for (int ch = 1; ch <= 2; ch++)
		{
			if (!logic[ch - 1] || !(ch_state[ch - 1].decode_on || want_i2c == 1))
			{
				continue;
			}
			std::vector<double>* data
			    = librador_get_digital_data(ch, poll_time_window, poll_num_samples, 0);
			if (data == nullptr)
			{
				// Disconnected / no sample stream yet
				ok = false;
			}
		}

		// Collect decoded text. librador returns a pointer into the decoder's
		// internal buffer (convertedStream_string) — owned by librador, must
		// NOT be freed, and only valid until the next call, so copy it out
		// immediately (Brent renders it in the same frame instead)
		for (int ch = 1; ch <= 2; ch++)
		{
			ChannelDecode& state = ch_state[ch - 1];
			if (!state.decode_on)
			{
				continue;
			}
			bool parity_check = true;
			char* decoded = librador_get_uart_string(ch, &parity_check);
			if (decoded != nullptr)
			{
				state.parity_ok = parity_check;
				AppendNewOutput(decoded, state.last_fetch, state.console);
			}
		}
		if (want_i2c == 1)
		{
			char* decoded = librador_get_i2c_string();
			if (decoded != nullptr)
			{
				AppendNewOutput(decoded, i2c_last_fetch, i2c_console);
			}
		}
		return ok;
	}

private:
	enum class Protocol
	{
		UART,
		I2C
	};

	// Per-channel UART decode state (index 0 = CH1, 1 = CH2)
	struct ChannelDecode
	{
		bool decode_on = false;
		int baud_idx = 0;      // 300 baud, mirrors Brent's default
		int parity_idx = 0;    // UartParity::None
		int char_mode_idx = 0; // 0 = ASCII (8-bit), 1 = Baudot (ITA2 5-bit)
		bool hex_display = false; // widget-side, ASCII mode only
		bool parity_ok = true; // last parity check reported by librador
		// Settings last pushed to librador (applied_baud_idx = -1 forces a push)
		bool applied_decode_on = false;
		int applied_baud_idx = -1;
		int applied_parity_idx = -1;
		int applied_char_mode_idx = -1;
		// Console scrollback (bounded to console_max_bytes)
		std::string console;
		std::string last_fetch;
		std::string hex_scratch;
	};

	/// <summary>
	/// Append the newly-decoded portion of a librador console snapshot to the
	/// scrollback. librador's serial buffer is append-only until it saturates
	/// (8 KB ring), after which the oldest characters fall off the front — so
	/// the new characters are whatever follows the longest overlap between
	/// the tail of the previous snapshot and the head of the current one.
	/// </summary>
	static void AppendNewOutput(
	    const char* fetched, std::string& last_fetch, std::string& console)
	{
		std::string current(fetched);
		size_t overlap = std::min(last_fetch.size(), current.size());
		while (overlap > 0
		    && std::memcmp(last_fetch.data() + (last_fetch.size() - overlap),
		           current.data(), overlap)
		        != 0)
		{
			overlap--;
		}
		console.append(current, overlap, std::string::npos);
		// Bound the scrollback so an endless stream cannot grow without limit
		if (console.size() > console_max_bytes)
		{
			console.erase(0, console.size() - console_max_bytes);
		}
		last_fetch = std::move(current);
	}

	/// <summary>
	/// Render the UART controls and console for one logic channel
	/// </summary>
	void renderUartChannel(int ch)
	{
		ChannelDecode& state = ch_state[ch - 1];
		ImGui::PushID(ch);
		ImGui::SeparatorText(ch == 1 ? "CH1 UART" : "CH2 UART");
		ImGui::Checkbox("Decode", &state.decode_on);
		ImGui::SameLine();
		// Hex is a widget-side rendering of the raw bytes — meaningless for
		// Baudot, where librador already maps ITA2 symbols to characters
		bool baudot = state.char_mode_idx == 1;
		ImGui::BeginDisabled(baudot);
		ImGui::Checkbox("Hex", &state.hex_display);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::Text("Chars");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
		ImGui::Combo("##chars", &state.char_mode_idx, char_mode_labels,
		    IM_ARRAYSIZE(char_mode_labels));

		ImGui::Text("Baud");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
		ImGui::Combo("##baud", &state.baud_idx, baud_rate_labels,
		    IM_ARRAYSIZE(baud_rate_labels));
		ImGui::SameLine();
		ImGui::Text("Parity");
		ImGui::SameLine();
		// Parity selector turns red while librador reports failing parity
		// checks (Brent's draw_grabber)
		bool parity_bad = state.decode_on && !state.parity_ok;
		if (parity_bad)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
		}
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5);
		ImGui::Combo(
		    "##parity", &state.parity_idx, parity_labels, IM_ARRAYSIZE(parity_labels));
		if (parity_bad)
		{
			ImGui::PopStyleColor();
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "parity error");
		}

		if (state.decode_on)
		{
			renderConsole("##uart_console", DisplayText(state), state.console);
		}
		ImGui::PopID();
	}

	/// <summary>
	/// Render the I2C console (librador's i2cdecoder already formats its
	/// output as hex, so there is no hex toggle here)
	/// </summary>
	void renderI2c()
	{
		ImGui::SeparatorText("I2C");
		ImGui::TextDisabled("SDA = CH1, SCL = CH2");
		renderConsole("##i2c_console", i2c_console, i2c_console);
	}

	/// <summary>
	/// Console-style child window with auto-scroll and a Clear button
	/// (Brent's print_stream is the model)
	/// </summary>
	void renderConsole(const char* id, const std::string& text, std::string& console_to_clear)
	{
		if (ImGui::SmallButton("Clear"))
		{
			// last_fetch is kept, so already-seen output is not re-appended
			console_to_clear.clear();
		}
		ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 20, 25, 255));
		if (ImGui::BeginChild(id,
		        ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * console_rows),
		        ImGuiChildFlags_Borders))
		{
			ImGui::TextWrapped("%s", text.c_str());
			// Stick to the newest output unless the user has scrolled away
			if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
			{
				ImGui::SetScrollHereY(1.0f);
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
	}

	/// <summary>
	/// Text to show in a UART console: the raw decoded characters, or their
	/// hex rendering. The merged librador exposes no hex-mode switch
	/// (uartStyleDecoder::m_hexDisplay is private), so the widget formats the
	/// decoded characters itself. Hex applies to ASCII mode only — in Baudot
	/// mode librador emits already-translated ITA2 characters.
	/// </summary>
	const std::string& DisplayText(ChannelDecode& state)
	{
		if (!state.hex_display || state.char_mode_idx != 0)
		{
			return state.console;
		}
		static const char* hex_digits = "0123456789ABCDEF";
		state.hex_scratch.clear();
		state.hex_scratch.reserve(state.console.size() * 3);
		for (unsigned char c : state.console)
		{
			state.hex_scratch.push_back(hex_digits[c >> 4]);
			state.hex_scratch.push_back(hex_digits[c & 0xF]);
			state.hex_scratch.push_back(' ');
		}
		return state.hex_scratch;
	}

	InputsControl* inputs = nullptr;
	Protocol protocol_sel = Protocol::UART;
	int applied_i2c_decoding = -1; // -1 unknown (forces a push), 0 off, 1 on
	ChannelDecode ch_state[2];
	std::string i2c_console;
	std::string i2c_last_fetch;

	// Qt preset baud list (also Brent's)
	static constexpr int baud_rates[12] = { 300, 600, 1200, 2400, 4800, 9600,
		14400, 19200, 28800, 38400, 57600, 115200 };
	static constexpr const char* baud_rate_labels[12] = { "300", "600", "1200",
		"2400", "4800", "9600", "14400", "19200", "28800", "38400", "57600",
		"115200" };
	static constexpr UartParity parities[3] = { UartParity::None,
		UartParity::Even, UartParity::Odd };
	static constexpr const char* parity_labels[3] = { "None", "Even", "Odd" };
	// Character coding (UartSettings::mode): 8-bit ASCII or 5-bit ITA2 telex
	static constexpr UartMode char_modes[2] = { UartMode::Standard8Bit,
		UartMode::Baudot };
	static constexpr const char* char_mode_labels[2] = { "ASCII", "Baudot" };

	static constexpr size_t console_max_bytes = 16384; // scrollback bound
	static constexpr int console_rows = 8;
	// Digital poll shape: Brent's plot polls with its x-span (initial 0.5 s),
	// GRAPH_SAMPLES = 512 and delay 0
	static constexpr double poll_time_window = 0.5;
	static constexpr int poll_num_samples = 512;
};
