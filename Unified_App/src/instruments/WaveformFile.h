#pragma once

#include "platform/paths.h"

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

/// <summary>
/// Loader for the Qt app's .tlw arbitrary-waveform files (bundled in
/// assets/waveforms/, discovered via assets/waveforms/_list.wfl).
///
/// .tlw format (plain ASCII, ported from Desktop_Interface
/// functiongencontrol.cpp SingleChannelController::waveformName):
///   line 1: sample count N (redundant with line 3, but cross-checked)
///   line 2: divisibility D — how many times the wave may be halved by
///           dropping every 2nd sample before it stops being representative.
///           The driver T-stretches (decimates) by up to D-1 when
///           N * frequency would exceed the 1 MSPS DAC ceiling.
///   line 3: N tab-separated samples, each 0..255 (255 = amplitude+offset V)
///
/// Frequency limits derived exactly as the Qt app does:
///   maxFreq = DAC_SPS / (N >> (D-1))            (fastest playable after
///                                                maximum decimation)
///   minFreq = CLOCK_FREQ / 1024 / 65535 / N     (slowest expressible with
///                                                the Xmega timer: largest
///                                                clock divider x 16-bit
///                                                period)
///
/// These are bundled read-only assets: any parse failure is a packaging bug
/// and throws std::runtime_error (loud failure, per codebase philosophy).
/// </summary>
class WaveformFile
{
public:
	// Hardware constants (Desktop_Interface xmega.h / librador usbcallhandler.h)
	static constexpr double DAC_SPS = 1000000.0; // Xmega DAC sample ceiling
	static constexpr double CLOCK_FREQ = 48000000.0; // XMEGA_MAIN_FREQ
	// librador's per-channel buffer size (FGEN_MAX_SAMPLES macro in
	// usbcallhandler.h — different name here to dodge the macro).
	static constexpr int FGEN_BUFFER_SAMPLES = 512;

	std::string name;
	int divisibility = 1;
	std::vector<uint8_t> samples;

	double maxFreq() const
	{
		return DAC_SPS / static_cast<double>(samples.size() >> (divisibility - 1));
	}

	double minFreq() const
	{
		return CLOCK_FREQ / 1024.0 / 65535.0 / static_cast<double>(samples.size());
	}

	/// <summary>
	/// Load and parse assets/waveforms/<name>.tlw.
	/// </summary>
	static WaveformFile load(const std::string& name)
	{
		return parse(name, loadAsset("waveforms/" + name + ".tlw"));
	}

	/// <summary>
	/// Waveform names listed in assets/waveforms/_list.wfl, one per line
	/// (port of espoComboBox::readWaveformList).
	/// </summary>
	static std::vector<std::string> availableWaveforms()
	{
		std::vector<std::string> names;
		for (const std::string& line : splitLines(loadAsset("waveforms/_list.wfl")))
		{
			if (!line.empty())
				names.push_back(line);
		}
		if (names.empty())
			throw std::runtime_error("waveforms/_list.wfl lists no waveforms");
		return names;
	}

	/// <summary>
	/// Parse a .tlw already in memory. Throws std::runtime_error on any
	/// malformed content, naming the offending file.
	/// </summary>
	static WaveformFile parse(const std::string& name, const std::vector<unsigned char>& raw)
	{
		auto fail = [&name](const std::string& why) -> std::runtime_error {
			return std::runtime_error("waveforms/" + name + ".tlw: " + why);
		};

		std::vector<std::string> lines = splitLines(raw);
		if (lines.size() < 3)
			throw fail("expected 3 lines (length, divisibility, samples), got "
			    + std::to_string(lines.size()));

		WaveformFile wf;
		wf.name = name;

		const long length = parseInt(lines[0], name, "length");
		wf.divisibility = static_cast<int>(parseInt(lines[1], name, "divisibility"));

		if (length < 1 || length > FGEN_BUFFER_SAMPLES)
			throw fail("length " + std::to_string(length) + " outside [1, "
			    + std::to_string(FGEN_BUFFER_SAMPLES) + "]");
		if (wf.divisibility < 1)
			throw fail("divisibility " + std::to_string(wf.divisibility) + " < 1");
		if ((length >> (wf.divisibility - 1)) < 1)
			throw fail("divisibility " + std::to_string(wf.divisibility)
			    + " over-decimates a " + std::to_string(length) + "-sample wave");

		// Line 3: tab-separated sample bytes
		wf.samples.reserve(length);
		const std::string& data = lines[2];
		size_t pos = 0;
		while (pos <= data.size())
		{
			size_t tab = data.find('\t', pos);
			if (tab == std::string::npos)
				tab = data.size();
			const long sample
			    = parseInt(data.substr(pos, tab - pos), name, "sample value");
			if (sample < 0 || sample > 255)
				throw fail("sample " + std::to_string(sample) + " outside [0, 255]");
			wf.samples.push_back(static_cast<uint8_t>(sample));
			pos = tab + 1;
		}

		// Qt cross-checks the header length against the sample list; so do we.
		if (static_cast<long>(wf.samples.size()) != length)
			throw fail("sample count mismatch: header says " + std::to_string(length)
			    + ", found " + std::to_string(wf.samples.size()));

		return wf;
	}

private:
	// Split on '\n', trimming trailing '\r' (tolerate CRLF) and dropping a
	// trailing empty line from a terminating newline.
	static std::vector<std::string> splitLines(const std::vector<unsigned char>& raw)
	{
		std::vector<std::string> lines;
		std::string current;
		for (unsigned char c : raw)
		{
			if (c == '\n')
			{
				lines.push_back(current);
				current.clear();
			}
			else if (c != '\r')
			{
				current.push_back(static_cast<char>(c));
			}
		}
		if (!current.empty())
			lines.push_back(current);
		return lines;
	}

	// Strict base-10 integer parse: the whole token must be consumed.
	static long parseInt(const std::string& token, const std::string& name, const char* what)
	{
		char* end = nullptr;
		const long value = std::strtol(token.c_str(), &end, 10);
		if (token.empty() || end != token.c_str() + token.size())
			throw std::runtime_error("waveforms/" + name + ".tlw: malformed "
			    + std::string(what) + " '" + token + "'");
		return value;
	}
};
