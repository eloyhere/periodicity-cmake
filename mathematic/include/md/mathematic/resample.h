/// @file   resample.h
/// @brief  Rational sample-rate conversion by polyphase filtering, with the
///         anti-alias prototype and a rate-to-ratio helper.
///

#pragma once

#include <md/mathematic/frequency.h>
#include <md/mathematic/sample.h>
#include <md/mathematic/window.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace md::mathematic {

	/// @brief  A windowed-sinc prototype for rational resampling.
	/// @param  up        Upsampling factor L, already reduced against @p down.
	/// @param  down      Downsampling factor M, already reduced against @p up.
	/// @param  halfTaps  Half the prototype length; the prototype has 2*halfTaps+1 taps.
	/// @return The prototype at the upsampled rate, scaled so the DC gain equals L.
	/// @note   The cutoff sits at the lower of the two Nyquist limits, so the same
	///         prototype both removes the images created by upsampling and prevents
	///         aliasing from downsampling.
	template<typename Number = double>
	std::vector<Number> resamplePrototype(const std::size_t up, const std::size_t down,
		const std::size_t halfTaps) {
		const std::size_t taps = 2 * halfTaps + 1;
		const Number scale = static_cast<Number>(std::max(up, down));

		std::vector<Number> prototype(taps, Number(0));
		const std::vector<Number> weights = makeWindow<Number>(WindowType::Hamming, taps);
		Number total = Number(0);
		for (std::size_t index = 0; index < taps; ++index) {
			const Number offset = static_cast<Number>(index) - static_cast<Number>(halfTaps);
			const Number argument = offset / scale;
			const Number sinc = (argument == Number(0))
				? Number(1)
				: static_cast<Number>(std::sin(Pi<Number> * argument)) / (Pi<Number> * argument);
			prototype[index] = sinc * weights[index];
			total += prototype[index];
		}
		if (total == Number(0)) {
			throw std::domain_error("resamplePrototype: the prototype has zero DC gain");
		}
		const Number gain = static_cast<Number>(up) / total;
		for (Number& value : prototype) {
			value *= gain;
		}
		return prototype;
	}

	/// @brief  Resamples a signal by the rational factor up / down.
	/// @param  signal    Input samples.
	/// @param  up        Upsampling factor L.
	/// @param  down      Downsampling factor M.
	/// @param  halfTaps  Half the anti-alias prototype length; 16 is a good default.
	/// @return The resampled signal, ceil(size * L / M) samples long.
	/// @throws std::invalid_argument  When a factor or the tap count is zero.
	/// @note   Samples outside the input are treated as zero, so the first and last
	///         few outputs carry an edge transient exactly as filtfilt does. The
	///         prototype group delay of halfTaps samples is compensated, which keeps
	///         output sample n aligned with input time n * M / L; without that the
	///         resampled stream would be late by a fixed number of samples, and every
	///         event-to-signal alignment downstream would inherit the offset.
	template<typename Number = double>
	std::vector<Number> resample(const std::vector<Number>& signal, std::size_t up, std::size_t down,
		const std::size_t halfTaps = 16) {
		if (up == 0 || down == 0) {
			throw std::invalid_argument("resample: the up and down factors must be positive");
		}
		if (halfTaps == 0) {
			throw std::invalid_argument("resample: at least one tap pair is required");
		}
		if (signal.empty()) {
			return {};
		}

		const std::size_t divisor = std::gcd(up, down);
		up /= divisor;
		down /= divisor;

		const std::vector<Number> prototype = resamplePrototype<Number>(up, down, halfTaps);
		const std::size_t taps = prototype.size();

		// Split the prototype into L phases: phase p holds taps p, p+L, p+2L, ...
		const std::size_t perPhase = (taps + up - 1) / up;
		std::vector<std::vector<Number>> bank(up, std::vector<Number>(perPhase, Number(0)));
		for (std::size_t index = 0; index < taps; ++index) {
			bank[index % up][index / up] = prototype[index];
		}

		const std::size_t outputLength = (signal.size() * up + down - 1) / down;
		std::vector<Number> result(outputLength, Number(0));
		const long long inputLength = static_cast<long long>(signal.size());

		for (std::size_t index = 0; index < outputLength; ++index) {
			// Advance by the half-tap delay so the prototype centre lands on the output time.
			const std::size_t position = index * down + halfTaps;
			const std::size_t phase = position % up;
			const long long base = static_cast<long long>((position - phase) / up);
			Number total = Number(0);
			for (std::size_t tap = 0; tap < perPhase; ++tap) {
				const long long source = base - static_cast<long long>(tap);
				if (source < 0 || source >= inputLength) {
					continue;
				}
				total += signal[static_cast<std::size_t>(source)] * bank[phase][tap];
			}
			result[index] = total;
		}
		return result;
	}

	/// @brief  Approximates a real number by a fraction with bounded denominator.
	/// @param  value           Number to approximate.
	/// @param  maxDenominator  Largest denominator accepted.
	/// @param  numerator       Receives the numerator.
	/// @param  denominator     Receives the denominator.
	/// @note   Continued fractions; the result is the best approximation with a
	///         denominator at or below the bound.
	inline void approximateRatio(const double value, const std::size_t maxDenominator,
		std::size_t& numerator, std::size_t& denominator) {
		if (!(value > 0.0) || maxDenominator == 0) {
			throw std::invalid_argument("approximateRatio: the value and bound must be positive");
		}
		double remaining = value;
		std::size_t previousNumerator = 0;
		std::size_t previousDenominator = 1;
		std::size_t currentNumerator = 1;
		std::size_t currentDenominator = 0;

		for (int iteration = 0; iteration < 64; ++iteration) {
			const double whole = std::floor(remaining);
			const std::size_t nextNumerator = static_cast<std::size_t>(whole) * currentNumerator
				+ previousNumerator;
			const std::size_t nextDenominator = static_cast<std::size_t>(whole) * currentDenominator
				+ previousDenominator;
			if (nextNumerator > maxDenominator || nextDenominator > maxDenominator) {
				break;
			}
			previousNumerator = currentNumerator;
			previousDenominator = currentDenominator;
			currentNumerator = nextNumerator;
			currentDenominator = nextDenominator;
			const double fraction = remaining - whole;
			if (fraction < 1.0e-12) {
				break;
			}
			remaining = 1.0 / fraction;
		}

		numerator = std::max<std::size_t>(currentNumerator, 1);
		denominator = std::max<std::size_t>(currentDenominator, 1);
	}

	/// @brief  Resamples from one sampling rate to another.
	/// @param  signal     Input samples.
	/// @param  from       Rate the samples were taken at.
	/// @param  to         Rate to convert to.
	/// @param  halfTaps   Half the anti-alias prototype length.
	/// @return The resampled signal.
	/// @throws std::invalid_argument  When a rate is not usable.
	template<typename Number = double>
	std::vector<Number> resampleTo(const std::vector<Number>& signal, const SampleRate<Number>& from,
		const SampleRate<Number>& to, const std::size_t halfTaps = 16) {
		if (!from.isValid() || !to.isValid()) {
			throw std::invalid_argument("resampleTo: both rates must be positive and finite");
		}
		const double ratio = static_cast<double>(to.hertz() / from.hertz());
		std::size_t up = 1;
		std::size_t down = 1;
		approximateRatio(ratio, 1000, up, down);
		return resample(signal, up, down, halfTaps);
	}
}
