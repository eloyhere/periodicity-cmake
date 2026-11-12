/// @file   ssvpe.h
/// @brief  Steady-state visual evoked potential decoding: canonical correlation
///         with sine/cosine references, and the filter-bank variant.
///

#pragma once

#include <md/mathematic/decomposition.h>
#include <md/mathematic/filter.h>
#include <md/mathematic/frequency.h>
#include <md/mathematic/matrix.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace md::mathematic {

	/// @brief  Builds the sine/cosine references of one stimulus frequency.
	/// @param  frequency   Stimulus frequency.
	/// @param  sampleRate  Sampling rate in hertz.
	/// @param  samples     Number of samples to generate.
	/// @param  harmonics   Number of harmonics, counting the fundamental.
	/// @return 2 * harmonics rows: sine then cosine for each harmonic.
	/// @throws std::invalid_argument  When the sample count or harmonic count is zero.
	/// @note   The references are the classic SSVEP template: a periodic response is
	///         a sum of sines and cosines, and CCA asks how much of the EEG lies in
	///         that subspace.
	template<typename Number = double>
	std::vector<std::vector<Number>> ssvpeReference(const Frequency<Number>& frequency,
		const Number& sampleRate, const std::size_t samples, const unsigned harmonics = 3) {
		if (samples == 0) {
			throw std::invalid_argument("ssvpeReference: at least one sample is required");
		}
		if (harmonics == 0) {
			throw std::invalid_argument("ssvpeReference: at least one harmonic is required");
		}
		if (!(sampleRate > Number(0))) {
			throw std::invalid_argument("ssvpeReference: the sample rate must be positive");
		}

		std::vector<std::vector<Number>> reference(2 * harmonics, std::vector<Number>(samples, Number(0)));
		for (unsigned harmonic = 1; harmonic <= harmonics; ++harmonic) {
			const Number omega = DoublePi<Number> * frequency.hertz() * static_cast<Number>(harmonic) / sampleRate;
			for (std::size_t sample = 0; sample < samples; ++sample) {
				const Number phase = omega * static_cast<Number>(sample);
				reference[2 * (harmonic - 1)][sample] = static_cast<Number>(std::sin(phase));
				reference[2 * (harmonic - 1) + 1][sample] = static_cast<Number>(std::cos(phase));
			}
		}
		return reference;
	}

	/// @brief  The canonical correlation between a montage and one stimulus frequency.
	/// @param  channels    One row of samples per channel.
	/// @param  frequency   Stimulus frequency to test.
	/// @param  sampleRate  Sampling rate in hertz.
	/// @param  harmonics   Number of harmonics in the reference.
	/// @return The largest canonical correlation, in [0, 1]; zero when a channel is flat.
	/// @throws std::invalid_argument  When the montage is empty or ragged.
	/// @note   Solved as A w = lambda C w with A = Cxy Cyy^-1 Cyx, so the cost is one
	///         small generalized eigenproblem rather than a full CCA implementation.
	template<typename Number = double>
	Number canonicalCorrelation(const std::vector<std::vector<Number>>& channels,
		const Frequency<Number>& frequency, const Number& sampleRate, const unsigned harmonics = 3) {
		if (channels.empty()) {
			throw std::invalid_argument("canonicalCorrelation: the montage is empty");
		}
		const std::size_t samples = channels.front().size();
		if (samples == 0) {
			throw std::invalid_argument("canonicalCorrelation: the channels are empty");
		}
		for (const auto& channel : channels) {
			if (channel.size() != samples) {
				throw std::invalid_argument("canonicalCorrelation: the channels differ in length");
			}
		}

		const std::vector<std::vector<Number>> reference = ssvpeReference(frequency, sampleRate, samples, harmonics);
		const std::size_t count = channels.size();
		const std::size_t references = reference.size();

		// Covariances, with the mean removed so a direct-current offset cannot inflate them.
		std::vector<Number> channelMeans(count, Number(0));
		for (std::size_t index = 0; index < count; ++index) {
			Number total = Number(0);
			for (const Number& value : channels[index]) {
				total += value;
			}
			channelMeans[index] = total / static_cast<Number>(samples);
		}
		std::vector<Number> referenceMeans(references, Number(0));
		for (std::size_t index = 0; index < references; ++index) {
			Number total = Number(0);
			for (const Number& value : reference[index]) {
				total += value;
			}
			referenceMeans[index] = total / static_cast<Number>(samples);
		}

		Matrix<Number> cross(count, references, Number(0));
		Matrix<Number> metric(count, count, Number(0));
		Matrix<Number> referenceMetric(references, references, Number(0));
		for (std::size_t first = 0; first < count; ++first) {
			for (std::size_t refIndex = 0; refIndex < references; ++refIndex) {
				Number total = Number(0);
				for (std::size_t sample = 0; sample < samples; ++sample) {
					total += (channels[first][sample] - channelMeans[first])
						* (reference[refIndex][sample] - referenceMeans[refIndex]);
				}
				cross(first, refIndex) = total / static_cast<Number>(samples);
			}
			for (std::size_t second = first; second < count; ++second) {
				Number total = Number(0);
				for (std::size_t sample = 0; sample < samples; ++sample) {
					total += (channels[first][sample] - channelMeans[first])
						* (channels[second][sample] - channelMeans[second]);
				}
				metric(first, second) = total / static_cast<Number>(samples);
				metric(second, first) = metric(first, second);
			}
		}
		for (std::size_t first = 0; first < references; ++first) {
			for (std::size_t second = first; second < references; ++second) {
				Number total = Number(0);
				for (std::size_t sample = 0; sample < samples; ++sample) {
					total += (reference[first][sample] - referenceMeans[first])
						* (reference[second][sample] - referenceMeans[second]);
				}
				referenceMetric(first, second) = total / static_cast<Number>(samples);
				referenceMetric(second, first) = referenceMetric(first, second);
			}
		}

		const Matrix<Number> referenceInverse = inverse(referenceMetric);

		// A = Cxy Cyy^-1 Cyx, assembled directly so no matrix product helper is needed.
		Matrix<Number> left(count, references, Number(0));
		for (std::size_t row = 0; row < count; ++row) {
			for (std::size_t column = 0; column < references; ++column) {
				Number total = Number(0);
				for (std::size_t inner = 0; inner < references; ++inner) {
					total += cross(row, inner) * referenceInverse(inner, column);
				}
				left(row, column) = total;
			}
		}
		Matrix<Number> numerator(count, count, Number(0));
		for (std::size_t row = 0; row < count; ++row) {
			for (std::size_t column = row; column < count; ++column) {
				Number total = Number(0);
				for (std::size_t inner = 0; inner < references; ++inner) {
					total += left(row, inner) * cross(column, inner);
				}
				numerator(row, column) = total;
				numerator(column, row) = total;
			}
		}

		Number trace = Number(0);
		for (std::size_t index = 0; index < count; ++index) {
			trace += metric(index, index);
		}
		if (!(trace > Number(0))) {
			return Number(0);
		}
		// A ridge keeps the generalized problem regular when channels are collinear.
		const Number ridge = trace / static_cast<Number>(count) * static_cast<Number>(1e-9);
		for (std::size_t index = 0; index < count; ++index) {
			metric(index, index) += ridge;
		}

		const EigenDecomposition<Number> spectrum = generalizedEigen(numerator, metric);
		const Number largest = std::min(std::max(spectrum.values.back(), Number(0)), Number(1));
		return static_cast<Number>(std::sqrt(largest));
	}

	/// @brief  The outcome of an SSVEP decision.
	///
	/// @tparam Number  Floating-point type used for the scores.
	template<typename Number = double>
	struct SsvpeResult {
		std::size_t index = 0;			///< Index of the winning candidate.
		Number score = Number(0);		///< Score of the winner.
		std::vector<Number> scores;		///< Score of every candidate, in input order.
	};

	/// @brief  Decides which of several stimulation frequencies the subject follows.
	/// @param  channels    One row of samples per channel.
	/// @param  candidates  Stimulus frequencies to test.
	/// @param  sampleRate  Sampling rate in hertz.
	/// @param  harmonics   Harmonics per reference.
	/// @param  bands       Zero for plain CCA; a positive count enables the filter bank.
	/// @return The winning candidate and every score.
	/// @throws std::invalid_argument  When the montage or candidate list is empty.
	/// @note   With a filter bank the score is sum_k w_k rho_k^2 with
	///         w_k = k^-1.25 + 0.25, which is the usual FBCCA weighting: lower
	///         sub-bands carry more of the steady-state response.
	template<typename Number = double>
	SsvpeResult<Number> classifySsvpe(const std::vector<std::vector<Number>>& channels,
		const std::vector<Frequency<Number>>& candidates, const Number& sampleRate,
		const unsigned harmonics = 3, const unsigned bands = 0) {
		if (channels.empty()) {
			throw std::invalid_argument("classifySsvpe: the montage is empty");
		}
		if (candidates.empty()) {
			throw std::invalid_argument("classifySsvpe: the candidate list is empty");
		}

		SsvpeResult<Number> result;
		result.scores.assign(candidates.size(), Number(0));

		if (bands == 0) {
			for (std::size_t index = 0; index < candidates.size(); ++index) {
				const Number correlation = canonicalCorrelation(channels, candidates[index], sampleRate, harmonics);
				result.scores[index] = correlation * correlation;
			}
		}
		else {
			const Number nyquist = sampleRate / Number(2);
			const Number ceiling = std::min(Number(90), nyquist * static_cast<Number>(0.9));
			std::vector<std::vector<std::vector<Number>>> filtered(bands);
			for (unsigned band = 0; band < bands; ++band) {
				const Number lower = Number(6) + Number(4) * static_cast<Number>(band);
				// A sub-band whose lower edge has reached the ceiling carries no signal;
				// it still scores zero rather than throwing, so the weighting stays fixed.
				if (!(lower < ceiling) || !(ceiling > Number(0))) {
					continue;
				}
				const auto sections = butterworthBandPass<Number>(4, Frequency<Number>::fromHertz(lower),
					Frequency<Number>::fromHertz(ceiling), sampleRate);
				filtered[band].reserve(channels.size());
				for (const auto& channel : channels) {
					filtered[band].push_back(filterSignal(sections, channel));
				}
			}

			for (std::size_t index = 0; index < candidates.size(); ++index) {
				Number total = Number(0);
				for (unsigned band = 0; band < bands; ++band) {
					if (filtered[band].empty()) {
						continue;
					}
					const Number correlation = canonicalCorrelation(filtered[band], candidates[index],
						sampleRate, harmonics);
					const Number weight = static_cast<Number>(std::pow(static_cast<double>(band + 1), -1.25))
						+ static_cast<Number>(0.25);
					total += weight * correlation * correlation;
				}
				result.scores[index] = total;
			}
		}

		const auto best = std::max_element(result.scores.begin(), result.scores.end());
		result.index = static_cast<std::size_t>(best - result.scores.begin());
		result.score = *best;
		return result;
	}
}
