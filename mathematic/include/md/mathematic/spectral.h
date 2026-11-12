/// @file   spectral.h
/// @brief  Window functions, the periodogram, Welch's averaged periodogram and
///         the spectrogram, built on the transform engine in sampler.h.

#pragma once

#include <md/mathematic/sampler.h>
#include <md/mathematic/spectrum.h>
#include <md/mathematic/window.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace md::mathematic {

	// The window catalogue, the Bessel helper and the two gains all live in
	// window.h; this header only consumes them so there is exactly one definition
	// of every window formula in the library.

	/// @brief  Builds a symmetric window of a requested shape.
	/// @param  type       Window shape, from window.h.
	/// @param  size       Number of samples; a single sample yields one.
	/// @param  parameter  Shape parameter; the beta of the Kaiser window.
	/// @return One weight per sample, symmetric about the centre.
	/// @note   Thin adapter over makeWindow() so the periodogram, Welch and
	///         spectrogram paths keep their existing call shape while the formula
	///         itself has a single home.
	template<typename Number = double>
	std::vector<Number> windowFunction(const WindowType type, const std::size_t size,
		const Number& parameter = Zero<Number>) {
		WindowParameters<Number> settings;
		settings.beta = (parameter == Zero<Number>) ? settings.beta : parameter;
		return makeWindow(type, size, settings);
	}

	/// @brief  The equivalent noise bandwidth of a window, measured in bins.
	/// @param  weights  Window samples.
	/// @return size * sum(w^2) / sum(w)^2.
	/// @note   Delegates to window.h so the definition is only stated once.
	template<typename Number = double>
	Number noiseBandwidth(const std::vector<Number>& weights) {
		return equivalentNoiseBandwidth(weights);
	}

	/// @brief  The periodogram of a real signal, an estimate of its power spectral density.
	/// @param  signal       Real samples.
	/// @param  sampleRate   Sampling rate in hertz.
	/// @param  windowType   Window shape; Hann by default.
	/// @return A one-sided density in units squared per hertz, including the
	///         folding factor of two for the interior bins.
	/// @throws std::invalid_argument  When the rate is not positive or the
	///         signal holds fewer than two samples.
	template<typename Number = double>
	PowerSpectrum<Number> periodogram(const std::vector<Number>& signal, const Number& sampleRate,
		const WindowType windowType = WindowType::Hann) {
		if (!(sampleRate > Zero<Number>)) {
			throw std::invalid_argument("periodogram: the sample rate must be positive");
		}
		if (signal.size() < 2) {
			throw std::invalid_argument("periodogram: at least two samples are required");
		}

		const std::size_t count = signal.size();
		const std::vector<Number> weights = windowFunction<Number>(windowType, count);

		Number mean = Zero<Number>;
		for (const Number& value : signal) {
			mean += value;
		}
		mean /= static_cast<Number>(count);

		Number windowPower = Zero<Number>;
		std::vector<Number> windowed(count);
		for (std::size_t index = 0; index < count; ++index) {
			windowed[index] = (signal[index] - mean) * weights[index];
			windowPower += weights[index] * weights[index];
		}

		const FourierPlan<Number> plan = FourierPlan<Number>::forSize(count);
		const std::vector<Complex<Number>> bins = plan.transformReal(windowed);
		const Number scale = One<Number> / (sampleRate * windowPower);

		std::vector<Number> frequencies(bins.size());
		std::vector<Number> powers(bins.size());
		for (std::size_t index = 0; index < bins.size(); ++index) {
			const bool folded = index != 0 && !(count % 2 == 0 && index == count / 2);
			const Number factor = folded ? static_cast<Number>(2) : One<Number>;
			frequencies[index] = static_cast<Number>(index) * sampleRate / static_cast<Number>(count);
			powers[index] = bins[index].magnitudeSquared() * scale * factor;
		}
		return PowerSpectrum<Number>(std::move(frequencies), std::move(powers));
	}

	/// @brief  Welch's averaged periodogram.
	/// @param  signal            Real samples.
	/// @param  sampleRate        Sampling rate in hertz.
	/// @param  segmentLength     Samples per segment; at most the signal length.
	/// @param  overlapFraction   Fractional overlap between segments, in [0, 1).
	/// @param  windowType        Window shape.
	/// @return The average of the segment periodograms.
	/// @throws std::invalid_argument  On an invalid rate, a segment shorter than
	///         two samples or longer than the signal, or an overlap outside [0, 1).
	template<typename Number = double>
	PowerSpectrum<Number> welch(const std::vector<Number>& signal, const Number& sampleRate,
		const std::size_t segmentLength, const Number& overlapFraction = static_cast<Number>(0.5),
		const WindowType windowType = WindowType::Hann) {
		if (!(sampleRate > Zero<Number>)) {
			throw std::invalid_argument("welch: the sample rate must be positive");
		}
		if (segmentLength < 2 || segmentLength > signal.size()) {
			throw std::invalid_argument("welch: the segment length must lie between two and the signal length");
		}
		if (!(overlapFraction >= Zero<Number>) || !(overlapFraction < One<Number>)) {
			throw std::invalid_argument("welch: the overlap fraction must lie in [0, 1)");
		}

		const std::size_t hop = std::max<std::size_t>(1, static_cast<std::size_t>(
			std::llround(static_cast<double>(segmentLength) * (1.0 - static_cast<double>(overlapFraction)))));

		std::vector<PowerSpectrum<Number>> segments;
		for (std::size_t start = 0; start + segmentLength <= signal.size(); start += hop) {
			const std::vector<Number> segment(signal.begin() + static_cast<std::ptrdiff_t>(start),
				signal.begin() + static_cast<std::ptrdiff_t>(start + segmentLength));
			segments.push_back(periodogram(segment, sampleRate, windowType));
		}

		if (segments.empty()) {
			throw std::invalid_argument("welch: the signal does not hold a single full segment");
		}

		const std::size_t bins = segments.front().size();
		std::vector<Number> frequencies(bins);
		std::vector<Number> powers(bins, Zero<Number>);
		for (std::size_t index = 0; index < bins; ++index) {
			frequencies[index] = segments.front().frequency(index);
		}
		for (const PowerSpectrum<Number>& segment : segments) {
			for (std::size_t index = 0; index < bins; ++index) {
				powers[index] += segment.power(index);
			}
		}
		const Number divisor = static_cast<Number>(segments.size());
		for (Number& value : powers) {
			value /= divisor;
		}
		return PowerSpectrum<Number>(std::move(frequencies), std::move(powers));
	}

	/// @brief  A sequence of Welch spectra over successive segments.
	/// @param  signal           Real samples.
	/// @param  sampleRate       Sampling rate in hertz.
	/// @param  segmentLength    Samples per segment.
	/// @param  overlapFraction  Fractional overlap between segments.
	/// @param  windowType       Window shape.
	/// @return One spectrum per segment, in time order.
	template<typename Number = double>
	std::vector<PowerSpectrum<Number>> spectrogram(const std::vector<Number>& signal, const Number& sampleRate,
		const std::size_t segmentLength, const Number& overlapFraction = static_cast<Number>(0.5),
		const WindowType windowType = WindowType::Hann) {
		if (segmentLength < 2 || segmentLength > signal.size()) {
			throw std::invalid_argument("spectrogram: the segment length must lie between two and the signal length");
		}
		if (!(overlapFraction >= Zero<Number>) || !(overlapFraction < One<Number>)) {
			throw std::invalid_argument("spectrogram: the overlap fraction must lie in [0, 1)");
		}

		const std::size_t hop = std::max<std::size_t>(1, static_cast<std::size_t>(
			std::llround(static_cast<double>(segmentLength) * (1.0 - static_cast<double>(overlapFraction)))));

		std::vector<PowerSpectrum<Number>> result;
		for (std::size_t start = 0; start + segmentLength <= signal.size(); start += hop) {
			const std::vector<Number> segment(signal.begin() + static_cast<std::ptrdiff_t>(start),
				signal.begin() + static_cast<std::ptrdiff_t>(start + segmentLength));
			result.push_back(periodogram(segment, sampleRate, windowType));
		}
		return result;
	}
}
