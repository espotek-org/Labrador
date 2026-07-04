#pragma once
#include <vector>
#include "librador.h"
#include <algorithm>
#include <chrono>
#include "fftw3.h"
#include <complex>
#include <array>
#include <float.h>
#include "TVD.hpp"
#define _USE_MATH_DEFINES
#include "math.h"
#include <cmath>
#include <limits>
#include <numeric>
class OSCControl;

class OscData
{
public:
	OscData(int channel)
	    : channel(channel)
	{
		data_ft_in = (double*)fftw_malloc(sizeof(double) * ft_size);
		data_ft_out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * ft_size);
		data_ft_in_filtered = (double*)fftw_malloc(sizeof(double) * ft_size);
		data_ft_out_filtered = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * ft_size);
		plan = fftw_plan_dft_r2c_1d(ft_size, data_ft_in, data_ft_out,FFTW_ESTIMATE);
		reverse_plan = fftw_plan_dft_c2r_1d(
		    ft_size, data_ft_out_filtered, data_ft_in_filtered, FFTW_ESTIMATE);
		// init mini buffer to all 1.65 (adc_center)
		mini_buffer.resize(int(max_sample_rate * mini_buffer_length));
		std::fill(mini_buffer.begin(), mini_buffer.end(), 1.65);
		data_ft_out_normalized.resize(ft_size);
	}
	void SetRawData() // sets the raw_data vector to be used for signal properties (to be changed soon)
	{
		if (!paused)
		{

			std::vector<double>* raw_data_ptr = librador_get_analog_data_by_rate(
			    channel, ft_time_window, ft_sample_rate, delay_s, filter_mode);
			if (raw_data_ptr)
			{
				raw_data = *raw_data_ptr;
				ApplyDisplayTransform(raw_data);
			}
			else
			{
				raw_data = {};
			}
			raw_time_step = 1 / ft_sample_rate;
			double time_step = raw_time_step;
			raw_time.resize(raw_data.size());
			std::generate(raw_time.begin(), raw_time.end(),
			    [n = 0, &time_step]() mutable
			    { return n++ * time_step; });
		}
	}
	void SetExtendedData() // sets the extended_data vector to be used for plotting, extended meaning including the trigger timeout
	{
		if (!paused)
		{
			extended_data_sample_rate_hz = CalculateSampleRate();
			double sample_rate_hz = extended_data_sample_rate_hz;
			double ext_time_window = 0;
			if (time_max < trigger_time_plot)
			{
				ext_time_window = trigger_timeout + trigger_time_plot - time_min;
			}
			else if (time_min > trigger_time_plot)
			{
				ext_time_window = time_max - trigger_time_plot + trigger_timeout;
			}
			else
			{
				ext_time_window = time_max - time_min + trigger_timeout;
			}
			std::vector<double>* extended_data_ptr = librador_get_analog_data_by_rate(channel,
				ext_time_window, sample_rate_hz, delay_s, filter_mode);
			if (extended_data_ptr)
			{
				extended_data = *extended_data_ptr;
				std::reverse(extended_data.begin(), extended_data.end());
				ApplyDisplayTransform(extended_data);
			}
			else
			{
				extended_data = {};
			}
		}
	}
	std::vector<double> GetData()
	{
		return data;
	}
	void SetData() // sets the data vector for plotting, this is exactly what is plotted
	{
		if (!paused)
		{
			double sample_rate_hz = extended_data_sample_rate_hz;
			if (extended_data.size() == 0)
			{
				data = {};
			}
			else if (trigger_on)
			{
				data = std::vector<double>(
					extended_data.begin() + (trigger_time_since_ext_start + time_min - trigger_time_plot) * sample_rate_hz,
					extended_data.begin() + (trigger_time_since_ext_start + time_min - trigger_time_plot + time_window) * sample_rate_hz);
			}
			else
			{
				data = std::vector<double>(
					extended_data.end() - time_window * sample_rate_hz,
					extended_data.end()
				);
			}
			this->time_step = time_window / data.size();
			double time_step = time_window / data.size();
			time.resize(data.size());
			auto& local_time_start = time_min;
			std::generate(time.begin(), time.end(),
			    [n = 0, &time_step, &local_time_start]() mutable
			    { return n++ * time_step + local_time_start; });
		}
	}
	// SetData overload to set data directly (used for math channel)
	void SetData(std::vector<double> data)
	{
		if (!paused)
		{
			this->data = data;
			this->time_step = time_window / data.size();
			double time_step = time_window / data.size();
			time.resize(data.size());
			auto& local_time_start = time_min;
			std::generate(time.begin(), time.end(),
				[n = 0, &time_step, &local_time_start]() mutable
				{ return n++ * time_step + local_time_start; });
		}
	}
	std::vector<double> GetTime()
	{
		return time;
	}
	double GetTimeStep()
	{
		return time_step;
	}
	void SetTime(double time_min, double time_max)
	{
		this->time_min = time_min;
		this->time_max = time_max;
		time_window = time_max - time_min;
	}
	// SetTime overload to set time directly (used for math channel)
	void SetTime(double time_min, double time_max, std::vector<double> time)
	{
		this->time_min = time_min;
		this->time_max = time_max;
		time_window = time_max - time_min;
		this->time = time;
		if (time.size() != 0)
		{
			this->time_step = time[1] - time[0];
		}
		else
		{
			this->time_step = 0;
		}
	}
	void SetTriggerOn(bool trigger)
	{
		trigger_on = trigger;
	}
	double GetTriggerTime(bool trigger,
		constants::TriggerType trigger_type,
		double trigger_level, double trigger_hysteresis)
	{
		trigger_on = trigger;
		double sample_rate_hz = extended_data_sample_rate_hz;
		int trigger_start_index = 0;
		int trigger_end_index = 0;
		std::vector<double> temp_data = {};
		if (!paused)
		{
			temp_data = extended_data;
			if (time_max > trigger_time_plot)
			{
				trigger_start_index = (int)temp_data.size() - 1 - (int)((time_max - trigger_time_plot) * sample_rate_hz);
			}
			else
			{
				trigger_start_index = (int)temp_data.size() - 1;
			}
			trigger_end_index = trigger_start_index - (int)(trigger_timeout * sample_rate_hz) + 2; // keep original +2

			if (trigger && time_window > 0 && !extended_data.empty())
			{
				// ---- Minimal additions begin ----
				// Build the exact segment we search over (ensure i-1 safe at the end of the loop)
				const int seg_begin = std::max(trigger_end_index - 1, 0);
				const int seg_end = std::max(std::min(trigger_start_index, (int)temp_data.size() - 1), 0);
				const int seg_len = seg_end - seg_begin + 1;

				double vpp = 0.0;
				if (seg_len >= 2) {
					// Copy, denoise, compute Vpp on denoised
					std::vector<double> seg;
					seg.reserve((size_t)seg_len);
					for (int i = seg_begin; i <= seg_end; ++i)
						seg.push_back(temp_data[(size_t)i]);

					const double lambda_tv = 0.12; // tune externally if desired
					std::vector<double> seg_dn = tv_denoise(seg, lambda_tv);

					auto mm = std::minmax_element(seg_dn.begin(), seg_dn.end());
					vpp = std::max(1e-12, *mm.second - *mm.first);

					// Write back denoised segment so the existing loop uses it unchanged
					for (int j = 0; j < seg_len; ++j)
						temp_data[(size_t)(seg_begin + j)] = seg_dn[(size_t)j];
				}
				else {
					vpp = 1e-12; // degenerate window; avoid zero
				}
				// ---- Minimal additions end ----

				switch (trigger_type)
				{
				case constants::TriggerType::RISING_EDGE:
				{
					// hysteresis is now a fraction of Vpp
					double hysteresis_level = trigger_level - (trigger_hysteresis * vpp);
					bool trigger_flag = false;
					for (int i = trigger_start_index; i > trigger_end_index; i--)
					{
						if (temp_data[(size_t)i] > trigger_level
							&& temp_data[(size_t)(i - 1)] < trigger_level)
						{
							trigger_time_since_ext_start = i / sample_rate_hz; // time in seconds after beginning of extended_data
							trigger_flag = true;
						}
						if (trigger_flag&& temp_data[(size_t)i] < hysteresis_level)
						{
							return trigger_time_since_ext_start;
						}
					}
					break;
				}
				case constants::TriggerType::FALLING_EDGE:
				{
					// hysteresis is now a fraction of Vpp
					double hysteresis_level = trigger_level + (trigger_hysteresis * vpp);
					bool trigger_flag = false;
					for (int i = trigger_start_index; i > trigger_end_index; i--)
					{
						if (temp_data[(size_t)i] < trigger_level && temp_data[(size_t)(i - 1)] > trigger_level)
						{
							trigger_time_since_ext_start = i / sample_rate_hz; // time in seconds after beginning of extended_data
							trigger_flag = true;
						}
						if (trigger_flag && temp_data[(size_t)i] > hysteresis_level)
						{
							return trigger_time_since_ext_start;
						}
					}
					break;
				}
				default:
				{
					trigger_time_since_ext_start = trigger_start_index / sample_rate_hz;
					return trigger_time_since_ext_start; // trigger as late as possible
				}
				}
				trigger_time_since_ext_start = trigger_end_index / sample_rate_hz;
				return trigger_time_since_ext_start; // trigger as late as possible
			}
			else
			{
				trigger_time_since_ext_start = trigger_start_index / sample_rate_hz;
				return trigger_time_since_ext_start; // trigger as late as possible
			}
		}
		return trigger_time_since_ext_start;
	}

	void SetTriggerTime(double trigger_time)
	{
		if (!paused)
		{
			this->trigger_time_since_ext_start = trigger_time;
		}
	}
	void SetTriggerTimeout(double trigger_timeout)
	{
		this->trigger_timeout = trigger_timeout;
	}
	void SetTriggerTimePlot(double trigger_time_plot)
	{
		this->trigger_time_plot = trigger_time_plot;
	}
	void SetPaused(bool paused)
	{
		this->paused = paused;
	}
	// ===== Eye diagram =====
	// Builds up to max_traces overlaid sweeps of this channel from
	// extended_data (already display-transformed, chronological), each
	// aligned so a trigger crossing sits at t = 0. The crossing search is
	// the same scheme as GetTriggerTime: level crossing re-armed at a
	// hysteresis level expressed as a fraction of the (TV-denoised) Vpp;
	// detection runs on the denoised copy while the traces themselves are
	// cut from the real samples. Each sweep spans one estimated unit
	// interval either side of its trigger (Qt's eye plots one symbol either
	// side of the symbol boundary, isodriver.cpp plot()); the unit interval
	// is estimated as the median spacing between consecutive triggers.
	// Returns the number of traces built (newest trigger first). Call
	// EyeTime()/EyeTraces() for the shared time base and the sweeps.
	// When paused the previous eye is kept, like every other Set*/Get* here.
	int GetEyeDiagram(constants::TriggerType trigger_type, double trigger_level,
	    double trigger_hysteresis, int max_traces)
	{
		if (!paused)
		{
			eye_time.clear();
			eye_traces.clear();
			const double sample_rate_hz = extended_data_sample_rate_hz;
			const std::vector<double>& src = extended_data; // chronological
			if (src.size() >= 8 && sample_rate_hz > 0 && max_traces > 0)
			{
				// Denoise for edge detection only (same lambda as the
				// trigger search in GetTriggerTime)
				const double lambda_tv = 0.12;
				std::vector<double> dn = tv_denoise(src, lambda_tv);
				auto mm = std::minmax_element(dn.begin(), dn.end());
				const double vpp = std::max(1e-12, *mm.second - *mm.first);

				// All hysteresis-validated trigger crossings, oldest first
				std::vector<int> trig_idx;
				const bool rising = trigger_type != constants::TriggerType::FALLING_EDGE;
				const double rearm_level = rising
				    ? trigger_level - trigger_hysteresis * vpp
				    : trigger_level + trigger_hysteresis * vpp;
				bool armed = rising ? dn[0] < rearm_level : dn[0] > rearm_level;
				for (size_t i = 1; i < dn.size(); i++)
				{
					const bool crossed = rising
					    ? (dn[i - 1] < trigger_level && dn[i] > trigger_level)
					    : (dn[i - 1] > trigger_level && dn[i] < trigger_level);
					if (armed && crossed)
					{
						trig_idx.push_back((int)i);
						armed = false;
					}
					else if (!armed
					    && (rising ? dn[i] < rearm_level : dn[i] > rearm_level))
					{
						armed = true;
					}
				}

				if (trig_idx.size() >= 2)
				{
					// Unit-interval estimate: median trigger-to-trigger gap
					std::vector<int> gaps;
					gaps.reserve(trig_idx.size() - 1);
					for (size_t i = 1; i < trig_idx.size(); i++)
						gaps.push_back(trig_idx[i] - trig_idx[i - 1]);
					std::nth_element(
					    gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
					const int half_window = std::max(gaps[gaps.size() / 2], 2);
					const int seg_len = 2 * half_window + 1;

					// Shared time base centred on the trigger instant
					eye_time.resize(seg_len);
					for (int j = 0; j < seg_len; j++)
						eye_time[j] = (j - half_window) / sample_rate_hz;

					// Newest triggers first, matching the plot trigger which
					// locks onto the most recent crossing
					for (auto it = trig_idx.rbegin();
					     it != trig_idx.rend() && (int)eye_traces.size() < max_traces;
					     ++it)
					{
						const int c = *it;
						if (c - half_window < 0 || c + half_window >= (int)src.size())
							continue; // sweep would run off the buffer
						eye_traces.emplace_back(src.begin() + (c - half_window),
						    src.begin() + (c + half_window + 1));
					}
					if (eye_traces.empty())
						eye_time.clear();
				}
			}
		}
		return (int)eye_traces.size();
	}
	const std::vector<double>& EyeTime() const { return eye_time; }
	const std::vector<std::vector<double>>& EyeTraces() const { return eye_traces; }
	// Per-channel display transform (probe attenuation + offset). Qt parity
	// (isodriver.cpp frameActionGeneric):
	//     displayed = measured / attenuation + offset
	// A pure display transform: every voltage vector fetched from librador
	// for plotting/analysis passes through it, so the plot, trigger search,
	// auto trigger level, signal properties, exports and spectrum all live in
	// the same displayed-volts domain. The auto-gain mini buffer is the one
	// deliberate exception — it must stay in raw ADC volts.
	// PlotWidget::UpdateOscData pushes the OSCControl state in each frame.
	void SetDisplayTransform(double attenuation, double offset)
	{
		this->display_attenuation = attenuation;
		this->display_offset = offset;
	}
	int GetDataSize()
	{
		return data.size();
	}
	double GetDataMax()
	{
		if (data.size() != 0)
		{
			return *std::max_element(data.begin(), data.end());
		}
		else
		{
			return DBL_MIN; // if there is no data, we want anything that compares with it to have a higher max
		}
			
	}
	double GetDataMin()
	{
		if (data.size() != 0)
		{
			return *std::min_element(data.begin(), data.end());
		}
		else
		{
			return DBL_MAX; // if there is no data, we want anything that compares with it to have a lower min
		}
	}
	void SetPeriodicData()
	{
		if (!paused)
		{
			int num_periods = 10;
			double sample_rate_hz = CalculateSampleRate();
			double periodic_time_window = GetTimeBetweenTriggers() * num_periods;
			std::vector<double>* periodic_data_ptr = librador_get_analog_data_by_rate(
				channel, periodic_time_window, sample_rate_hz, delay_s, filter_mode);
			if (periodic_data_ptr)
			{
				periodic_data = *periodic_data_ptr;
				std::reverse(periodic_data.begin(), periodic_data.end());
				ApplyDisplayTransform(periodic_data);
			}
			else
			{
				periodic_data = {};
			}
		}
	}
	/// <summary>
	///  Get time between triggers
	/// </summary>
	/// <returns></returns>
	double GetTimeBetweenTriggers()
	{
		// i'm going to use raw_data vector here for maximum accuracy, this is the one we used for fft - it has high sample rate and a large time window
		double period = 0;
		if (raw_data.size() != 0)
		{
			double trigger_level;
			double hysteresis_level;
			double hysteresis_factor = 0.3;
			// calculate average of signal for setting of trigger level
			double average = 0;
			for (int i = 0; i < raw_data.size(); i++)
			{
				average += raw_data[i];
			}
			average = average / raw_data.size();
			trigger_level = average;
			// calculate min for setting of hysteresis level
			double min = *std::min_element(raw_data.begin(), raw_data.end());
			hysteresis_level
				= trigger_level + (min - trigger_level) * hysteresis_factor;
			// use rising edge trigger functionality to find all phase-aligned points with the trigger
			std::vector<double> phase_aligned_t = {};
			bool hysteresis_hit = false;
			for (int i = 0; i < raw_data.size() - 1; i++)
			{
				if (raw_data[i] < hysteresis_level && raw_data[i + 1] > hysteresis_level)
				{
					hysteresis_hit = true;
				}
				if (hysteresis_hit && raw_data[i] > trigger_level)
				{
					phase_aligned_t.push_back(raw_time[i]);
					hysteresis_hit = false;
				}
			}
			// calculate average of difference between phase-aligned times to get the
			// average period
			double sum_of_periods = 0;
			if (phase_aligned_t.size() > 1)
			{
				for (int i = 0; i < phase_aligned_t.size() - 1; i++)
				{
					sum_of_periods += phase_aligned_t[i + 1] - phase_aligned_t[i];
				}
				period = sum_of_periods / (phase_aligned_t.size() - 1);
			}
		}
		return period;
	}
	double GetVoltageRange() // specific function for finding Vpp that attempts to remove outliers
	{
		if (periodic_data.size() == 0)
		{
			return 0;
		}
		// apply total variation denoising (TVD) to denoise the signal
		double lambda = 0.1;
		std::vector<double> denoised_signal = tv_denoise(periodic_data, lambda);
		if (denoised_signal.size() > 0)
		{
			double max = *std::max_element(denoised_signal.begin(), denoised_signal.end());
			double min = *std::min_element(denoised_signal.begin(), denoised_signal.end());
			return max - min;
		}
		else
		{
			return 0;
		}

	}
	double GetAverage()
	{
		//i'm going to use vpp data since it should contain a whole number of periods
		if (periodic_data.size()==0)
		{
			return 0;
		}
		double sum = 0;
		for (int i = 0; i < periodic_data.size(); i++)
		{
			sum += periodic_data[i];
		}
		sum /= periodic_data.size();
		return sum;
	}
	void PerformSpectrumAnalysis(double sample_rate = 375000,
		double time_window = 5,
		int windowing_function = 0)       // 0=Hann, 1=Rectangular
	{
		// ---- 0) Acquire one record (single-block FFT, DSO-style) ----
		std::vector<double>* data_for_spectrum_ptr =
			librador_get_analog_data_by_rate(channel, time_window, sample_rate, delay_s, filter_mode);

		if (data_for_spectrum_ptr) {
			data_for_spectrum = *data_for_spectrum_ptr;
			std::reverse(data_for_spectrum.begin(), data_for_spectrum.end()); // keep your original behavior
			ApplyDisplayTransform(data_for_spectrum); // attenuation scales magnitudes; offset falls out with the mean
		}
		else {
			data_for_spectrum.clear();
		}

		const size_t L = data_for_spectrum.size();
		if (L == 0) {
			time_for_spectrum.clear();
			spectrum_freq.clear();
			spectrum_data.clear();
			return;
		}

		// ---- 1) Time axis for whole capture ----
		time_for_spectrum.resize(L);
		for (size_t i = 0; i < L; ++i)
			time_for_spectrum[i] = i / sample_rate;

		// ---- 2) Window (Rect by default; Hann if requested) ----
		std::vector<double> w(L, 1.0);
		if (windowing_function == 0 && L > 1) { // Hann
			const double inv = 1.0 / double(L - 1);
			for (size_t n = 0; n < L; ++n)
				w[n] = 0.5 * (1.0 - std::cos(2.0 * M_PI * n * inv));
		}

		// Coherent gain (sum of window) � used to correct tone amplitude
		double sumw = 0.0;
		for (double a : w) sumw += a;
		const double inv_sumw = (sumw != 0.0) ? (1.0 / sumw) : 0.0;

		// ---- 3) Detrend (remove mean) + apply window ----
		double mean = 0.0;
		for (double v : data_for_spectrum) mean += v;
		mean /= double(L);

		std::vector<double> in(L);
		for (size_t n = 0; n < L; ++n)
			in[n] = (data_for_spectrum[n] - mean) * w[n];

		// ---- 4) FFT (real->complex, one-sided) ----
		const bool   even = (L % 2 == 0);
		const size_t Nb = L / 2 + 1;                 // one-sided bins
		const double df = sample_rate / double(L); // frequency step

		spectrum_freq.resize(Nb);
		for (size_t k = 0; k < Nb; ++k)
			spectrum_freq[k] = k * df;

		fftw_complex* out = fftw_alloc_complex(Nb);
		fftw_plan plan = fftw_plan_dft_r2c_1d(int(L), in.data(), out, FFTW_ESTIMATE);
		fftw_execute(plan);
		fftw_destroy_plan(plan);

		// ---- 5) Scale to per-bin Vrms (DSO behaviour) ----
	// For a bin-centered sine with peak amplitude A_pk:
	//   |X[k]| ~ sum(w) * A_pk / 2  (FFTW forward is unscaled)
	// Single-sided interior bins doubled (×2); DC/Nyquist are not.
	// Vrms = A_pk / sqrt(2)  =>  Vrms = |X[k]| * (s / sum(w)) / sqrt(2)
		const double root2 = std::sqrt(2.0);

		spectrum_data.resize(Nb);         // per-bin Vrms (Volts)
		spectrum_data_db_v.resize(Nb);    // per-bin dBV
		spectrum_data_db_m.resize(Nb);    // per-bin dBm (re 1 mW into spectrum_load_ohms)

		for (size_t k = 0; k < Nb; ++k) {
			const double re = out[k][0];
			const double im = out[k][1];
			const double mag = std::hypot(re, im);           // |X[k]|
			const bool is_dc = (k == 0);
			const bool is_nyq = (even && k == Nb - 1);
			const double s = (is_dc || is_nyq) ? 1.0 : 2.0;  // single-sided factor

			const double vrms = (mag * (s * inv_sumw)) / root2; // per-bin Vrms
			spectrum_data[k] = vrms;
			spectrum_data_db_v[k] = vrms_to_dBV(vrms);
			spectrum_data_db_m[k] = vrms_to_dBm(vrms, spectrum_load_ohms);
		}

		fftw_free(out);

		// NOTE:
		// - 'spectrum_data'      : per-bin Vrms (V)
		// - 'spectrum_data_db_v' : per-bin dBV (20*log10(Vrms))
		// - 'spectrum_data_db_m' : per-bin dBm re 1 mW into spectrum_load_ohms
	}
	std::vector<double> GetSpectrumMag() { return spectrum_data; } // Vrms
	std::vector<double> GetSpectrumMagdBV() { return spectrum_data_db_v; }
	std::vector<double> GetSpectrumMagdBm() { return spectrum_data_db_m; }

	std::vector<double> GetSpectrumFreq() { return spectrum_freq; }

	std::vector<double>* GetSpectrumMag_p() { return &spectrum_data; } // Vrms
	std::vector<double>* GetSpectrumMagdBV_p() { return &spectrum_data_db_v; }
	std::vector<double>* GetSpectrumMagdBm_p() { return &spectrum_data_db_m; }
	std::vector<double>* GetSpectrumFreq_p() { return &spectrum_freq; }


	void ApplyFFT() //unused will remove
	{
		std::copy(raw_data.begin(), raw_data.end(), data_ft_in);
		fftw_execute(plan);
		double freq_step = 1 / ft_time_window;
		frequency_ft.resize(ft_size);
		std::generate(frequency_ft.begin(), frequency_ft.end(),
		    [n = 0, &freq_step]() mutable { return n++ * freq_step; });
		// Normalize FFT
		data_ft_out_normalized.resize(ft_size);
		for (int i = 0; i < ft_size; i++)
		{
			data_ft_out_normalized[i].real(data_ft_out[i][0] / (ft_size));
			data_ft_out_normalized[i].imag(data_ft_out[i][1] / (ft_size));
		}
	}
	double GetDominantFrequency() // unused will remove
	{
		std::vector<double> ft_mag;
		ApplyFFT();
		for (int i = 0; i < ft_size; i++)
		{
			ft_mag.push_back(std::abs(data_ft_out_normalized[i]));
		}
		if (ft_mag.size() > 0)
		{
			ft_mag[0] = 0; // ignore the zero frequency (offset)
			int freq_max_idx = std::max_element(ft_mag.begin(), ft_mag.end()) - ft_mag.begin();
			return frequency_ft[freq_max_idx];
		}
		else
		{
			return 0;
		}
	}
	double GetDCComponent() //unused will remove
	{
		return std::abs(data_ft_out_normalized[0]);
	}
	std::vector<double> GetFilteredData() // applies low pass filter, unused currently but kept just in case, unused will remove
	{
		double filter_cutoff = 50000; // hz
		for (int i = 0; i < raw_data.size(); i++)
		{
			if ( i < raw_data.size()/16)
			{
				data_ft_out_filtered[i][0] = data_ft_out[i][0];
				data_ft_out_filtered[i][1] = data_ft_out[i][1];
			}
			else
			{
				data_ft_out_filtered[i][0] = 0;
				data_ft_out_filtered[i][1] = 0;
			}
		}
		fftw_execute(reverse_plan);
		std::vector<double> filtered_data(data_ft_in_filtered, data_ft_in_filtered + ft_size);
		// scale by 1/N
		double norm_factor = 1.0 / filtered_data.size();
		std::transform(filtered_data.begin(), filtered_data.end(), filtered_data.begin(),
		    [&norm_factor](auto& c) { return c * norm_factor; });
		return filtered_data;
	}
	std::vector<double> GetMiniBuffer()
	{
		if (!paused)
		{
			FillMiniBuffer();
		}
		return mini_buffer;
	}

	int GetChannel() const
	{
		return channel;
	}
	std::vector<double> GetRawData() // unused will remove later
	{
		return raw_data;
	}
	// --------------------- Basic stats ---------------------

	double GetVmax() const {
		if (data.empty()) return NaN();
		return *std::max_element(data.begin(), data.end());
	}

	double GetVmin() const {
		if (data.empty()) return NaN();
		return *std::min_element(data.begin(), data.end());
	}

	double GetVpp() const {
		if (data.empty()) return NaN();
		double vmax = GetVmax();
		double vmin = GetVmin();
		if (!valid(vmax) || !valid(vmin)) return NaN();
		return vmax - vmin;
	}

	double GetVavg() const {
		if (data.empty()) return NaN();
		double sum = std::accumulate(data.begin(), data.end(), 0.0);
		return sum / (double)data.size();
	}

	double GetVrms() const {
		if (data.empty()) return NaN();
		// RMS of the signal as-is (includes DC)
		long double acc = 0.0L;
		for (double v : data) acc += (long double)v * (long double)v;
		double mean_sq = (double)(acc / (long double)data.size());
		return std::sqrt(mean_sq);
	}

	// --------------------- Period via mid-level crossings ---------------------

	// Assumes members: std::vector<double> time, data;
	// Helpers you likely already have:
	//   inline bool valid(double x); inline double NaN(); inline double wrap_deg(double a);

	double GetPeriod() const {
		// Use the overlap to be safe.
		const size_t N = std::min(time.size(), data.size());
		if (N < 8) return NaN();

		// Require strictly increasing timestamps.
		for (size_t i = 1; i < N; ++i)
			if (!(time[i] > time[i - 1])) return NaN();


		// ---- 1) DENOISE ----
		// Make a contiguous copy for TVD (in case 'data' has extra capacity or is a view).
		std::vector<double> periodic_data;
		periodic_data.assign(data.begin(), data.begin() + N);
		// You can tune lambda externally; 0.05–0.3 is a common sweet spot.
		double lambda = 0.1;
		std::vector<double> x = tv_denoise(periodic_data, lambda);   // denoised signal (length N expected)
		if (x.size() != N) return NaN();

		// ---- 2) MIDPOINT & HYSTERESIS FROM DENOISED + RESIDUAL NOISE ----
		// Compute vmin, vmax from the denoised signal (more stable than raw).
		auto [min_it, max_it] = std::minmax_element(x.begin(), x.end());
		const double vmin = *min_it;
		const double vmax = *max_it;
		if (!std::isfinite(vmin) || !std::isfinite(vmax)) return NaN();

		const double vpp = vmax - vmin;
		if (!(vpp > 0.0)) return NaN();

		const double vmid = 0.5 * (vmax + vmin);

		// Estimate residual noise from raw - denoised via MAD (robust).
		auto median_of = [](std::vector<double>& v)->double {
			if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
			size_t n = v.size() / 2;
			std::nth_element(v.begin(), v.begin() + n, v.end());
			double m = v[n];
			if ((v.size() & 1) == 0) {
				// even: also get the lower neighbor
				std::nth_element(v.begin(), v.begin() + n - 1, v.end());
				m = 0.5 * (m + v[n - 1]);
			}
			return m;
		};

		std::vector<double> r; r.reserve(N);
		for (size_t i = 0; i < N; ++i) r.push_back(periodic_data[i] - x[i]);

		double r_med = [&] {
			std::vector<double> tmp = r;
			return median_of(tmp);
		}();

		double mad = [&] {
			std::vector<double> tmp; tmp.reserve(N);
			for (double ri : r) tmp.push_back(std::abs(ri - r_med));
			return median_of(tmp);
		}();

		const double sigma = (std::isfinite(mad) ? 1.4826 * mad : 0.0);

		// Hysteresis: derive from noise and amplitude. Floor to avoid chatter on tiny signals.
		// Larger of (3σ) and (5% Vpp), with a very small absolute floor.
		const double h = std::max(std::max(3.0 * sigma, 0.05 * vpp), 1e-6);
		const double lo = vmid - h;

		// ---- 3) RISING vmid CROSSINGS ON DENOISED (RE-ARM BELOW lo) ----
		std::vector<double> t_cross; t_cross.reserve(N / 3);
		bool armed = (x[0] <= lo); // start armed if we begin below lo

		for (size_t i = 1; i < N; ++i) {
			const double x0 = x[i - 1];
			const double x1 = x[i];

			if (armed) {
				// rising crossing exactly at vmid (guarantees interpolation is inside segment)
				if (x0 <= vmid && x1 > vmid) {
					const double dx = x1 - x0;     // > 0 in normal rising case
					if (dx != 0.0) {
						// Optional shallow-edge guard: require slope big enough in volts/sample
						// (helps with ultra-low amplitude + noise). Tune 1e-4 as needed.
						if (dx > 1e-4 * vpp) {
							const double alpha = (vmid - x0) / dx; // in [0,1] by construction
							const double t = time[i - 1] + alpha * (time[i] - time[i - 1]);
							if (std::isfinite(t)) t_cross.push_back(t);
						}
					}
					armed = false; // disarm until we drop below lo again
				}
			}
			else {
				if (x1 < lo) armed = true; // re-arm when safely below lo
			}
		}

		// ---- 4) PERIOD FROM CROSSINGS (CENTERED OLS; FALLBACK FOR 2) ----
		const size_t M = t_cross.size();
		if (M < 2) return NaN();

		if (M == 2) {
			const double T = t_cross[1] - t_cross[0];
			return (std::isfinite(T) && T > 0.0) ? T : NaN();
		}

		// Centered least-squares slope of t_k ≈ t0 + k*T, k = 0..M-1
		const long double Mld = (long double)M;
		const long double mean_k = (Mld - 1.0L) * 0.5L;

		long double mean_t = 0.0L;
		for (size_t k = 0; k < M; ++k) mean_t += (long double)t_cross[k];
		mean_t /= Mld;

		long double cov_kt = 0.0L, var_k = 0.0L;
		for (size_t k = 0; k < M; ++k) {
			const long double kc = (long double)k - mean_k;
			const long double tc = (long double)t_cross[k] - mean_t;
			cov_kt += kc * tc;
			var_k += kc * kc;
		}

		if (var_k == 0.0L) return NaN();
		const double T = (double)(cov_kt / var_k);
		return (std::isfinite(T) && T > 0.0) ? T : NaN();
	}



	// --------------------- Absolute phase at f ≈ 1/T (ref at window center) ---------------------
	double GetPhaseDeg(double* freq_used_out = nullptr) const {
		const size_t N = std::min(time.size(), data.size());
		if (N < 8) {
			if (freq_used_out) *freq_used_out = NaN();
			return NaN();
		}
		for (size_t i = 1; i < N; ++i)
			if (!(time[i] > time[i - 1])) { if (freq_used_out) *freq_used_out = NaN(); return NaN(); }

		// Base frequency from period
		const double T = GetPeriod();
		if (!(valid(T) && T > 0.0)) { if (freq_used_out) *freq_used_out = NaN(); return NaN(); }
		const double f0 = 1.0 / T;

		// DC removal
		const double mean = GetVavg();
		if (!valid(mean)) { if (freq_used_out) *freq_used_out = NaN(); return NaN(); }

		// Reference time at window center to stabilize phase when the window slides
		const double t0 = 0.5 * (time.front() + time.back());

		// Hann window (weight)
		auto hann_w = [N](size_t n)->double {
			if (N < 2) return 1.0;
			return 0.5 * (1.0 - std::cos(2.0 * M_PI * (double)n / (double)(N - 1)));
		};

		// Small frequency refinement around f0 using a 3-point parabolic peak on |S(f)|:
		// Evaluate S at f0-df, f0, f0+df
		const double span = std::max(1e-12, time.back() - time.front());
		const double df = 1.0 / span;  // ~frequency bin of the aperture
		auto proj = [&](double f)->std::pair<double, double> {
			long double Re = 0.0L, Im = 0.0L;
			for (size_t n = 0; n < N; ++n) {
				const double x = data[n] - mean;
				const double w = hann_w(n);
				const double ang = 2.0 * M_PI * f * (time[n] - t0);  // center-referenced
				const double c = std::cos(ang), s = std::sin(ang);
				Re += (long double)(w * x) * (long double)c;
				Im -= (long double)(w * x) * (long double)s; // e^{-iωt}
			}
			return { (double)Re, (double)Im };
		};
		auto mag2 = [](double Re, double Im) { return Re * Re + Im * Im; };

		const auto [Re0m, Im0m] = proj(f0 - df);
		const auto [Re0, Im0] = proj(f0);
		const auto [Re0p, Im0p] = proj(f0 + df);

		const double A0m = mag2(Re0m, Im0m);
		const double A0 = mag2(Re0, Im0);
		const double A0p = mag2(Re0p, Im0p);

		// Parabolic interpolation of peak shift (Δ in bins of df)
		double delta = 0.0;
		const double denom = (A0m - 2.0 * A0 + A0p);
		if (std::fabs(denom) > 0.0) {
			delta = 0.5 * (A0m - A0p) / denom;  // in [-1, +1] ideally
			delta = std::max(-1.0, std::min(1.0, delta));
		}
		const double f1 = std::max(0.0, f0 + delta * df);

		// Final projection at refined f1 (still referenced to t0)
		const auto [Re, Im] = proj(f1);
		if (!(std::isfinite(Re) && std::isfinite(Im))) { if (freq_used_out) *freq_used_out = NaN(); return NaN(); }

		if (freq_used_out) *freq_used_out = f1;
		const double phi = std::atan2(Im, Re) * (180.0 / M_PI);
		return wrap_deg(phi);
	}


private:
	// time domain (plot) stuff
	double time_min = 0;
	double time_max = 0;
	double time_window = 0;
	double time_start = 0;
	double time_step = 0;
	std::vector<double> time = {};
	std::vector<double> data = {};
	std::vector<double> extended_data = {};
	std::vector<double> periodic_data = {}; // i create this vector to just contain n periods
	// eye diagram (see GetEyeDiagram): shared time base + overlaid sweeps
	std::vector<double> eye_time = {};
	std::vector<std::vector<double>> eye_traces = {};
	std::vector<double> fft_data_time_domain = {};
	int channel;
	int filter_mode = 0;
	double delay_s = 0;
	bool trigger_on = true;
	double trigger_timeout = 0.2; // seconds
	double trigger_time_since_ext_start = 0; // seconds, relative to start of extended_data vector
	double trigger_time_plot = 0; // seconds, time that trigger shows on plot
	double extended_data_sample_rate_hz = 0;
	int max_plot_samples = 2048;
	double max_sample_rate = 375000;
	// spectrum analyser
	std::vector<double> data_for_spectrum = {};
	std::vector<double> time_for_spectrum = {};
	std::vector<double> spectrum_data = {};      // per-bin Vrms (linear)
	std::vector<double> spectrum_data_db_v = {}; // per-bin dBV
	std::vector<double> spectrum_data_db_m = {}; // per-bin dBm
	std::vector<double> spectrum_freq = {};
	double spectrum_load_ohms = 50.0;           // dBm reference load
	// frequency stuff
	std::vector<double> raw_data = {};
	std::vector<double> raw_time = {}; // time associated with raw_data
	int ft_size = 16384*4; // 2^15
	double* data_ft_in; // raw_data copied to double array
	fftw_complex* data_ft_out; // fft is output into this complex number array
	std::vector<std::complex<double>> data_ft_out_normalized = {}; // std vector which we copy data_ft_out to, normalized, uses std::complex instead of fftw_complex which gives us handy functions
	std::vector<double> time_ft = {}; // currently unused as is the time vector for the resampling
	std::vector<double> frequency_ft = {};
	fftw_plan plan;
	fftw_complex* data_ft_out_filtered;
	double* data_ft_in_filtered;
	fftw_plan reverse_plan;
	double ft_sample_rate = max_sample_rate; // sets max frequency detection (must be a factor of the max sample rate)
	double ft_time_window = ft_size / ft_sample_rate; // sets frequency detection resolution (and min frequency detection)
	double raw_time_step = 1 / ft_sample_rate;
	// mini buffer (used for determining gain of oscilloscope)
	std::vector<double> mini_buffer = {};
	double mini_buffer_length = 0.05; // seconds
	int mini_buffer_next_index = 0;
	
	// osc control parameters
	bool paused = false;
	// display transform (probe attenuation divisor + additive volt offset),
	// see SetDisplayTransform
	double display_attenuation = 1.0;
	double display_offset = 0.0;
	void ApplyDisplayTransform(std::vector<double>& v) const
	{
		if (display_attenuation == 1.0 && display_offset == 0.0)
		{
			return;
		}
		for (double& s : v)
		{
			s = s / display_attenuation + display_offset;
		}
	}
	double CalculateSampleRate()
	{
		double sample_rate = max_plot_samples / time_window;
		for (auto i = constants::DIVISORS_375000.rbegin();
		     i != constants::DIVISORS_375000.rend(); ++i)
		{
			int divisor = *i;
			if (sample_rate > divisor)
			{
				return divisor;
			}
		}
		return 1;
	}
	// fast fourier transform works best (fastest) when have the same length vector every time, and even better if it is a power of 2
	// we create a function to resample to a constant size for use with the fft
	// specifically, this function works by taking linear interpolations between samples to obtain a piecewise continuous function
	// we can then just pluck values from this with the new sample rate
	void ResampleForFT() // currently unused as is quite inefficient, i set sizes manually to ideal for frequency analysis
	{
		if (data.size() > 1 && time.size()>1)
		{
			double step;
			step = raw_data.size() / double(ft_size);
			time_ft = {};
			frequency_ft = {};
			for (int i = 0; i < ft_size; i++)
			{
				double raw_idx = i * step;
				int raw_idx_left = int(std::floor(raw_idx));
				int raw_idx_right = int(std::ceil(raw_idx));
				if (raw_idx_right >= raw_data.size())
				{
					raw_idx_right = raw_data.size() - 1;
				}
				double data_slope = raw_data[raw_idx_right] - raw_data[raw_idx_left];
				double time_slope = raw_time[raw_idx_right] - raw_time[raw_idx_left];
				// set interpolated value
				data_ft_in[i] = raw_data[raw_idx_left] + data_slope * (raw_idx - raw_idx_left);
				time_ft.push_back(raw_time[raw_idx_left] + time_slope * (raw_idx - raw_idx_left));
				double delta_time = raw_time_step * step;
				frequency_ft.push_back(i / (ft_size * delta_time));
			}
		}
	}
	double GetComplexMagnitude(fftw_complex ft_sample) // for fftw_complex
	{
		double ft_sample_re = ft_sample[0];
		double ft_sample_im = ft_sample[1];
		double ft_sample_mag = std::sqrt(ft_sample[0] * ft_sample[0] + ft_sample[1] * ft_sample[1]);
		return ft_sample_mag;
	}
	void FillMiniBuffer() // used for auto osc gain and that's all
	{
		std::vector<double>* buffer_update_ptr = librador_get_analog_data_sincelast(
		    channel, 5, ft_sample_rate, delay_s, filter_mode);
		if (buffer_update_ptr)
		{
			int buffer_update_size = buffer_update_ptr->size();
			for (int i = 0; i < buffer_update_size; i++)
			{
				int mini_buffer_idx = (mini_buffer_next_index + i) % mini_buffer.size();
				mini_buffer[mini_buffer_idx]
				    = buffer_update_ptr->at(buffer_update_size - 1 - i);
			}
			mini_buffer_next_index
			    = (mini_buffer_next_index + buffer_update_size) % mini_buffer.size();
		}
			
	}

	// === Helpers ===
	static inline bool valid(double x) {
		return std::isfinite(x);
	}
	static inline double NaN() {
		return std::numeric_limits<double>::quiet_NaN();
	}
	static inline double wrap_deg(double a) {
		if (!std::isfinite(a)) return a;
		a = std::fmod(a + 180.0, 360.0);
		if (a < 0) a += 360.0;
		return a - 180.0;
	}
	// Vrms -> dBV and dBm helpers
	inline double vrms_to_dBV(double v) {
		const double eps = 1e-30;
		return 20.0 * std::log10(std::max(std::abs(v), eps)); // re 1 V
	}
	inline double vrms_to_dBm(double v, double R_ohm) {
		const double eps = 1e-30;
		double p_w = (v * v) / std::max(R_ohm, eps);           // P = V^2 / R
		return 10.0 * std::log10(std::max(p_w, eps) / 1e-3); // re 1 mW
	}
};


