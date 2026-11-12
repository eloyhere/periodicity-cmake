/// @file   spectrum.h
/// @brief  Containers for a power spectrum and for a complex spectrum, with
///         band integration, relative and logarithmic power and peak detection.

#pragma once

#include <md/mathematic/frequency.h>
#include <md/mathematic/numerical.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace md::mathematic {

	/// @brief  A real, non-negative power spectrum sampled on a frequency axis.
	///
	/// Bins are assumed to be uniformly spaced, which is what a Fourier transform
	/// produces. The container owns no estimation logic: periodogram() and
	/// welch() in spectral.h produce it, this type answers questions about it.
	///
	/// @tparam Number  Floating-point type used for frequencies and powers.
	template<typename Number = double>
	class PowerSpectrum {
	private:
		std::vector<Number> frequencyValues;	///< Bin frequencies in hertz, increasing.
		std::vector<Number> powerValues;		///< Power or power density per bin.

	public:
		/// @brief  Constructs an empty spectrum.
		PowerSpectrum() = default;

		/// @brief  Constructs a spectrum from two parallel arrays.
		/// @param  frequencies  Bin frequencies in hertz.
		/// @param  powers       Power per bin; must have the same size.
		/// @throws std::invalid_argument  When the arrays differ in size.
		PowerSpectrum(std::vector<Number> frequencies, std::vector<Number> powers)
			: frequencyValues(std::move(frequencies)), powerValues(std::move(powers)) {
			if (frequencyValues.size() != powerValues.size()) {
				throw std::invalid_argument("PowerSpectrum: the frequency and power arrays must have the same size");
			}
		}

		/// @brief  Number of bins.
		std::size_t size() const {
			return powerValues.size();
		}

		/// @brief  Reports whether the spectrum holds no bins.
		bool empty() const {
			return powerValues.empty();
		}

		/// @brief  The bin frequencies.
		const std::vector<Number>& frequencies() const {
			return frequencyValues;
		}

		/// @brief  The bin powers.
		const std::vector<Number>& powers() const {
			return powerValues;
		}

		/// @brief  The frequency of one bin.
		/// @param  index  Zero-based bin index.
		Number frequency(const std::size_t index) const {
			return frequencyValues.at(index);
		}

		/// @brief  The power of one bin.
		/// @param  index  Zero-based bin index.
		Number power(const std::size_t index) const {
			return powerValues.at(index);
		}

		/// @brief  Spacing between neighbouring bins.
		/// @return The first spacing, or zero when there are fewer than two bins.
		Number frequencyResolution() const {
			if (frequencyValues.size() < 2) {
				return Zero<Number>;
			}
			return frequencyValues[1] - frequencyValues[0];
		}

		/// @brief  The highest frequency in the spectrum.
		/// @return The last bin frequency, or zero for an empty spectrum.
		Number upperFrequency() const {
			return frequencyValues.empty() ? Zero<Number> : frequencyValues.back();
		}

		/// @brief  The integral of the spectrum over all bins.
		/// @return The sum of the bin powers times the resolution.
		Number totalPower() const {
			const Number resolution = frequencyResolution();
			Number total = Zero<Number>;
			for (const Number& value : powerValues) {
				total += value;
			}
			return total * resolution;
		}

		/// @brief  The integral of the spectrum inside a band.
		/// @param  band  Frequency band to integrate over.
		/// @return The sum of the powers of the bins inside the band, times the
		///         resolution; zero when the band contains no bin.
		Number bandPower(const FrequencyBand<Number>& band) const {
			const Number resolution = frequencyResolution();
			Number total = Zero<Number>;
			for (std::size_t index = 0; index < powerValues.size(); ++index) {
				if (band.contains(Frequency<Number>::fromHertz(frequencyValues[index]))) {
					total += powerValues[index];
				}
			}
			return total * resolution;
		}

		/// @brief  The share of the total power that falls inside a band.
		/// @param  band  Frequency band to measure.
		/// @return bandPower() divided by totalPower(), or zero when the total
		///         power vanishes.
		Number relativeBandPower(const FrequencyBand<Number>& band) const {
			const Number total = totalPower();
			if (total == Zero<Number>) {
				return Zero<Number>;
			}
			return bandPower(band) / total;
		}

		/// @brief  The band power expressed in decibels.
		/// @param  band       Frequency band to measure.
		/// @param  reference  Reference power, one by default.
		/// @return Ten times the base-ten logarithm of bandPower() / reference;
		///         negative infinity when the band power is zero.
		Number bandPowerDecibels(const FrequencyBand<Number>& band, const Number& reference = One<Number>) const {
			const Number power = bandPower(band);
			if (power <= Zero<Number> || reference <= Zero<Number>) {
				return -std::numeric_limits<Number>::infinity();
			}
			return static_cast<Number>(10) * static_cast<Number>(std::log10(power / reference));
		}

		/// @brief  The index of the strongest bin.
		/// @return The index of the largest power, or zero for an empty spectrum.
		std::size_t peakIndex() const {
			if (powerValues.empty()) {
				return 0;
			}
			std::size_t best = 0;
			for (std::size_t index = 1; index < powerValues.size(); ++index) {
				if (powerValues[index] > powerValues[best]) {
					best = index;
				}
			}
			return best;
		}

		/// @brief  The frequency of the strongest bin.
		Number peakFrequency() const {
			return empty() ? Zero<Number> : frequency(peakIndex());
		}

		/// @brief  Local maxima that stand out from their neighbourhood.
		/// @param  minimumProminence       Smallest rise over the smaller neighbour.
		/// @param  minimumFrequencyDistance  Smallest spacing between two reported peaks.
		/// @return Peak indices, strongest first.
		std::vector<std::size_t> peaks(const Number& minimumProminence = Zero<Number>,
			const Number& minimumFrequencyDistance = Zero<Number>) const {
			std::vector<std::size_t> candidates;
			for (std::size_t index = 1; index + 1 < powerValues.size(); ++index) {
				if (powerValues[index] > powerValues[index - 1] && powerValues[index] >= powerValues[index + 1]) {
					candidates.push_back(index);
				}
			}
			std::sort(candidates.begin(), candidates.end(), [this](const std::size_t left, const std::size_t right) {
				return powerValues[left] > powerValues[right];
			});

			std::vector<std::size_t> accepted;
			for (const std::size_t candidate : candidates) {
				const Number prominence = powerValues[candidate]
					- std::min(powerValues[candidate - 1], powerValues[candidate + 1]);
				if (prominence < minimumProminence) {
					continue;
				}
				bool tooClose = false;
				for (const std::size_t existing : accepted) {
					if (std::abs(frequencyValues[candidate] - frequencyValues[existing]) < minimumFrequencyDistance) {
						tooClose = true;
						break;
					}
				}
				if (!tooClose) {
					accepted.push_back(candidate);
				}
			}
			return accepted;
		}
	};

	/// @brief  A complex spectrum, retaining magnitude and phase per bin.
	///
	/// @tparam Number  Floating-point type used for frequencies and components.
	template<typename Number = double>
	class ComplexSpectrum {
	private:
		std::vector<Number> frequencyValues;			///< Bin frequencies in hertz.
		std::vector<Complex<Number>> binValues;			///< Complex value per bin.

	public:
		/// @brief  Constructs an empty spectrum.
		ComplexSpectrum() = default;

		/// @brief  Constructs a spectrum from two parallel arrays.
		/// @param  frequencies  Bin frequencies in hertz.
		/// @param  values       Complex value per bin; must have the same size.
		/// @throws std::invalid_argument  When the arrays differ in size.
		ComplexSpectrum(std::vector<Number> frequencies, std::vector<Complex<Number>> values)
			: frequencyValues(std::move(frequencies)), binValues(std::move(values)) {
			if (frequencyValues.size() != binValues.size()) {
				throw std::invalid_argument("ComplexSpectrum: the frequency and value arrays must have the same size");
			}
		}

		/// @brief  Number of bins.
		std::size_t size() const {
			return binValues.size();
		}

		/// @brief  Reports whether the spectrum holds no bins.
		bool empty() const {
			return binValues.empty();
		}

		/// @brief  The bin frequencies.
		const std::vector<Number>& frequencies() const {
			return frequencyValues;
		}

		/// @brief  The complex values.
		const std::vector<Complex<Number>>& values() const {
			return binValues;
		}

		/// @brief  The frequency of one bin.
		Number frequency(const std::size_t index) const {
			return frequencyValues.at(index);
		}

		/// @brief  The complex value of one bin.
		const Complex<Number>& value(const std::size_t index) const {
			return binValues.at(index);
		}

		/// @brief  The modulus of one bin.
		Number magnitude(const std::size_t index) const {
			return binValues.at(index).magnitude();
		}

		/// @brief  The phase of one bin.
		Angle<Number> phase(const std::size_t index) const {
			return binValues.at(index).argument();
		}

		/// @brief  The power of one bin.
		Number power(const std::size_t index) const {
			return binValues.at(index).magnitudeSquared();
		}

		/// @brief  The index of the strongest bin.
		std::size_t peakIndex() const {
			if (binValues.empty()) {
				return 0;
			}
			std::size_t best = 0;
			for (std::size_t index = 1; index < binValues.size(); ++index) {
				if (binValues[index].magnitudeSquared() > binValues[best].magnitudeSquared()) {
					best = index;
				}
			}
			return best;
		}

		/// @brief  The frequency of the strongest bin.
		Number peakFrequency() const {
			return empty() ? Zero<Number> : frequency(peakIndex());
		}

		/// @brief  Converts the modulus squared into a real power spectrum.
		PowerSpectrum<Number> powerSpectrum() const {
			std::vector<Number> powers(binValues.size());
			for (std::size_t index = 0; index < binValues.size(); ++index) {
				powers[index] = binValues[index].magnitudeSquared();
			}
			return PowerSpectrum<Number>(frequencyValues, std::move(powers));
		}
	};
}
