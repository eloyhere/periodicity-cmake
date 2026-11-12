/// @file   pac.h
/// @brief  Phase-amplitude coupling: the modulation index of Tort, the mean vector
///         length of Canolty, and the surrogate test that says whether either is real.
///

#pragma once

#include <md/mathematic/numerical.h>
#include <md/mathematic/sampler.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace md::mathematic {

	/// @brief  The measured coupling between a low-frequency phase and a high-frequency amplitude.
	///
	/// @tparam Number  Floating-point type used for the measures.
	template<typename Number = double>
	struct PacResult {
		Number modulationIndex = Number(0);	///< Tort's index, in [0, 1].
		Number meanVectorLength = Number(0);	///< Canolty's index, in [0, 1].
		std::vector<Number> distribution;		///< Normalised mean amplitude per phase bin.
		std::size_t samples = 0;				///< Number of samples that entered the measure.
	};

	/// @brief  The observed coupling together with its surrogate test.
	///
	/// @tparam Number  Floating-point type used for the measures.
	template<typename Number = double>
	struct PacSignificance {
		PacResult<Number> observed;	///< The measured coupling.
		Number pValue = Number(1);	///< Share of surrogates at least as large.
		Number zScore = Number(0);	///< Distance from the surrogate mean in surrogate standard deviations.
	};

	/// @brief  Measures phase-amplitude coupling between two band-limited signals.
	/// @param  low    Low-frequency signal, whose phase drives the coupling.
	/// @param  high   High-frequency signal, whose amplitude is tested.
	/// @param  bins   Phase bins; 18 gives 20 degrees per bin.
	/// @return The modulation index, the mean vector length and the phase-amplitude histogram.
	/// @throws std::invalid_argument  When the lengths disagree, a signal is too short
	///         or fewer than two bins are requested.
	/// @note   Both inputs must already be band-limited. Passing a broadband signal
	///         produces a phase that is meaningless, so the caller owns that decision.
	template<typename Number = double>
	PacResult<Number> phaseAmplitudeCoupling(const std::vector<Number>& low,
		const std::vector<Number>& high, const std::size_t bins = 18) {
		if (low.size() != high.size()) {
			throw std::invalid_argument("phaseAmplitudeCoupling: the signals differ in length");
		}
		if (low.size() < 4) {
			throw std::invalid_argument("phaseAmplitudeCoupling: at least four samples are required");
		}
		if (bins < 2) {
			throw std::invalid_argument("phaseAmplitudeCoupling: at least two bins are required");
		}

		const std::vector<Complex<Number>> phaseSource = Sampler<Number>::analyticSignal(low);
		const std::vector<Complex<Number>> amplitudeSource = Sampler<Number>::analyticSignal(high);

		std::vector<Number> sums(bins, Number(0));
		std::vector<Number> counts(bins, Number(0));
		Number amplitudeTotal = Number(0);
		Complex<Number> vector(Number(0), Number(0));

		for (std::size_t index = 0; index < low.size(); ++index) {
			const Number phase = static_cast<Number>(std::atan2(phaseSource[index].imaginary(),
				phaseSource[index].real()));
			const Number amplitude = amplitudeSource[index].magnitude();
			amplitudeTotal += amplitude;

			// Map the phase from [-pi, pi) onto [0, bins).
			Number normalised = (phase + Pi<Number>) / DoublePi<Number>;
			normalised = std::min(std::max(normalised, Number(0)), Number(1) - Number(1e-12));
			const std::size_t bin = std::min(static_cast<std::size_t>(normalised * static_cast<Number>(bins)),
				bins - 1);
			sums[bin] += amplitude;
			counts[bin] += Number(1);
			vector += Complex<Number>(amplitude * static_cast<Number>(std::cos(phase)),
				amplitude * static_cast<Number>(std::sin(phase)));
		}

		PacResult<Number> result;
		result.samples = low.size();
		result.distribution.assign(bins, Number(0));

		Number meanTotal = Number(0);
		for (std::size_t bin = 0; bin < bins; ++bin) {
			result.distribution[bin] = (counts[bin] > Number(0))
				? sums[bin] / counts[bin]
				: Number(0);
			meanTotal += result.distribution[bin];
		}
		if (!(meanTotal > Number(0))) {
			return result;
		}
		for (Number& value : result.distribution) {
			value /= meanTotal;
		}

		// Tort's modulation index: 1 minus the normalised entropy of the histogram.
		Number entropy = Number(0);
		for (const Number& value : result.distribution) {
			if (value > Number(0)) {
				entropy -= value * static_cast<Number>(std::log(value));
			}
		}
		const Number maximum = static_cast<Number>(std::log(static_cast<Number>(bins)));
		result.modulationIndex = (maximum > Number(0))
			? (maximum - entropy) / maximum
			: Number(0);
		result.modulationIndex = std::min(std::max(result.modulationIndex, Number(0)), Number(1));

		// Canolty's mean vector length, normalised by the total amplitude.
		result.meanVectorLength = (amplitudeTotal > Number(0))
			? vector.magnitude() / amplitudeTotal
			: Number(0);
		result.meanVectorLength = std::min(result.meanVectorLength, Number(1));
		return result;
	}

	/// @brief  Tests coupling against surrogates built by permuting blocks of the amplitude.
	/// @param  low         Low-frequency signal.
	/// @param  high        High-frequency signal.
	/// @param  bins        Phase bins.
	/// @param  surrogates  Number of permuted copies; 200 is a reasonable default.
	/// @param  seed        Random seed, so a report can be reproduced.
	/// @return The observed measures, the p value and the z score.
	/// @throws std::invalid_argument  When the surrogate count is zero.
	/// @note   A block permutation keeps the local spectrum but destroys the
	///         alignment between the amplitude envelope and the phase.
	/// @note   A circular shift was the first choice here and it is wrong for this
	///         measure: shifting a stationary amplitude envelope only rotates its
	///         distribution around the phase axis, and the modulation index is
	///         invariant under that rotation. Every surrogate then looks as strong
	///         as the observation and the test can never reach significance, however
	///         real the coupling is. Permuting blocks breaks the alignment instead of
	///         rotating it, so the null distribution actually collapses toward zero.
	template<typename Number = double>
	PacSignificance<Number> pacSurrogateTest(const std::vector<Number>& low,
		const std::vector<Number>& high, const std::size_t bins = 18,
		const std::size_t surrogates = 200, const std::uint32_t seed = 20261112u) {
		if (surrogates == 0) {
			throw std::invalid_argument("pacSurrogateTest: at least one surrogate is required");
		}

		PacSignificance<Number> result;
		result.observed = phaseAmplitudeCoupling(low, high, bins);
		const std::size_t samples = high.size();
		if (samples < 2) {
			return result;
		}

		std::mt19937 generator(seed);
		// The block must be shorter than the phase cycle, otherwise each block still
		// carries the whole phase-amplitude relation and the permutation changes
		// nothing. A few dozen samples is short enough for any theta-range drive.
		const std::size_t blockLength = std::max<std::size_t>(samples / 200, 4);
		const std::size_t blockCount = samples / blockLength;
		std::vector<std::size_t> order(blockCount);
		std::vector<Number> permuted(samples, Number(0));

		std::size_t atLeastAsLarge = 0;
		Number mean = Number(0);
		Number squares = Number(0);
		for (std::size_t iteration = 0; iteration < surrogates; ++iteration) {
			for (std::size_t index = 0; index < blockCount; ++index) {
				order[index] = index;
			}
			for (std::size_t index = order.size(); index > 1; --index) {
				std::uniform_int_distribution<std::size_t> pick(0, index - 1);
				std::swap(order[index - 1], order[pick(generator)]);
			}
			for (std::size_t block = 0; block < blockCount; ++block) {
				for (std::size_t offset = 0; offset < blockLength; ++offset) {
					permuted[block * blockLength + offset]
						= high[order[block] * blockLength + offset];
				}
			}
			// Any tail beyond the last whole block keeps its original samples.
			for (std::size_t index = blockCount * blockLength; index < samples; ++index) {
				permuted[index] = high[index];
			}

			const Number value = phaseAmplitudeCoupling(low, permuted, bins).modulationIndex;
			mean += value;
			squares += value * value;
			if (value >= result.observed.modulationIndex) {
				++atLeastAsLarge;
			}
		}

		mean /= static_cast<Number>(surrogates);
		const Number variance = squares / static_cast<Number>(surrogates) - mean * mean;
		const Number deviation = static_cast<Number>(std::sqrt(std::max(variance, Number(0))));
		result.pValue = static_cast<Number>(atLeastAsLarge + 1) / static_cast<Number>(surrogates + 1);
		result.zScore = (deviation > Number(0))
			? (result.observed.modulationIndex - mean) / deviation
			: Number(0);
		return result;
	}
}
