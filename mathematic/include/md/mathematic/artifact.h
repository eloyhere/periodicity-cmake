/// @file   artifact.h
/// @brief  Artifact suppression: subspace reconstruction (ASR) for large
///         transients, and regression of reference channels such as EOG.
///
/// Method note: the reconstruction here is the essence of ASR rather than the
/// full published pipeline. It learns a calibration covariance, projects each
/// window into that eigenbasis, and discounts the components whose variance has
/// grown beyond a threshold. The published algorithm additionally adapts the
/// threshold per subject and mixes the correction with a Kalman-style update; those
/// refinements change tuning, not the idea, and are left for when real recordings
/// are available to calibrate against.
///

#pragma once

#include <md/mathematic/decomposition.h>
#include <md/mathematic/matrix.h>
#include <md/mathematic/numerical.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace md::mathematic {

	/// @brief  Reconstructs large transients from the calibration subspace.
	///
	/// @tparam Number  Floating-point type used for the samples.
	template<typename Number = double>
	class ArtifactSubspace {
	private:
		std::vector<Number> eigenvalues;	///< Calibration variance per component, ascending.
		Matrix<Number> directions;			///< Eigenvectors as columns, matching the eigenvalues.
		std::size_t channels = 0;			///< Channel count the model expects.

	public:
		/// @brief  Learns the clean subspace from a calibration recording.
		/// @param  data       One row of samples per channel.
		/// @param  blockSize  Samples per block; must be at least two.
		/// @throws std::invalid_argument  When the montage is empty, ragged or too short.
		/// @throws std::domain_error  When the calibration covariance is degenerate.
		void calibrate(const std::vector<std::vector<Number>>& data, const std::size_t blockSize = 250) {
			if (data.empty()) {
				throw std::invalid_argument("ArtifactSubspace::calibrate: the montage is empty");
			}
			if (blockSize < 2) {
				throw std::invalid_argument("ArtifactSubspace::calibrate: the block size must be at least two");
			}
			channels = data.size();
			const std::size_t samples = data.front().size();
			for (const auto& channel : data) {
				if (channel.size() != samples) {
					throw std::invalid_argument("ArtifactSubspace::calibrate: the channels differ in length");
				}
			}
			if (samples < blockSize) {
				throw std::invalid_argument("ArtifactSubspace::calibrate: fewer samples than one block");
			}

			Matrix<Number> covariance(channels, channels, Number(0));
			std::size_t blocks = 0;
			for (std::size_t start = 0; start + blockSize <= samples; start += blockSize) {
				for (std::size_t first = 0; first < channels; ++first) {
					for (std::size_t second = first; second < channels; ++second) {
						Number total = Number(0);
						for (std::size_t offset = 0; offset < blockSize; ++offset) {
							total += data[first][start + offset] * data[second][start + offset];
						}
						covariance(first, second) += total / static_cast<Number>(blockSize);
						covariance(second, first) = covariance(first, second);
					}
				}
				++blocks;
			}
			if (blocks == 0) {
				throw std::invalid_argument("ArtifactSubspace::calibrate: no complete block is available");
			}
			for (std::size_t first = 0; first < channels; ++first) {
				for (std::size_t second = 0; second < channels; ++second) {
					covariance(first, second) /= static_cast<Number>(blocks);
				}
			}

			const EigenDecomposition<Number> spectrum = symmetricEigen(covariance);
			eigenvalues = spectrum.values;
			directions = spectrum.vectors;
			// A floor keeps the ratio well defined for components that are silent
			// during calibration, which happens whenever a channel is disconnected.
			const Number floor = std::max(eigenvalues.back() * static_cast<Number>(1e-9), Number(1e-12));
			for (Number& value : eigenvalues) {
				value = std::max(value, floor);
			}
		}

		/// @brief  The calibration eigenvalues, ascending.
		const std::vector<Number>& variances() const {
			return eigenvalues;
		}

		/// @brief  Reconstructs artifacts over overlapping windows and cross-fades them.
		/// @param  data       One row of samples per channel.
		/// @param  blockSize  Window length in samples; half of it is the hop.
		/// @param  threshold  A component is discounted when its window variance
		///                    exceeds this multiple of its calibration variance.
		/// @return The cleaned montage, same shape as the input.
		/// @throws std::logic_error  When the model has not been calibrated.
		/// @throws std::invalid_argument  When the montage shape does not match.
		std::vector<std::vector<Number>> apply(const std::vector<std::vector<Number>>& data,
			const std::size_t blockSize = 250, const Number& threshold = static_cast<Number>(5)) const {
			if (eigenvalues.empty()) {
				throw std::logic_error("ArtifactSubspace::apply: the model is not calibrated");
			}
			if (data.size() != channels) {
				throw std::invalid_argument("ArtifactSubspace::apply: the channel count does not match");
			}
			const std::size_t samples = data.front().size();
			for (const auto& channel : data) {
				if (channel.size() != samples) {
					throw std::invalid_argument("ArtifactSubspace::apply: the channels differ in length");
				}
			}
			if (blockSize < 2 || !(threshold > Number(0))) {
				throw std::invalid_argument("ArtifactSubspace::apply: the block size and threshold must be positive");
			}

			const std::size_t hop = std::max<std::size_t>(blockSize / 2, 1);
			std::vector<std::vector<Number>> cleaned(channels, std::vector<Number>(samples, Number(0)));
			std::vector<Number> weight(samples, Number(0));
			std::vector<Number> window(blockSize, Number(0));
			for (std::size_t offset = 0; offset < blockSize; ++offset) {
				window[offset] = static_cast<Number>(0.5)
					- static_cast<Number>(0.5) * static_cast<Number>(std::cos(
						DoublePi<Number> * static_cast<Number>(offset) / static_cast<Number>(blockSize - 1)));
			}
			const Number thresholdSquared = threshold * threshold;

			for (std::size_t start = 0; start < samples; start += hop) {
				const std::size_t length = std::min(blockSize, samples - start);
				if (length < 2) {
					break;
				}

				// Project onto the calibration basis: components = V^T X.
				std::vector<std::vector<Number>> components(channels, std::vector<Number>(length, Number(0)));
				for (std::size_t component = 0; component < channels; ++component) {
					for (std::size_t index = 0; index < length; ++index) {
						Number total = Number(0);
						for (std::size_t channel = 0; channel < channels; ++channel) {
							total += directions(channel, component) * data[channel][start + index];
						}
						components[component][index] = total;
					}
				}

				// Discount the components whose variance outgrew the calibration.
				for (std::size_t component = 0; component < channels; ++component) {
					Number total = Number(0);
					for (std::size_t index = 0; index < length; ++index) {
						total += components[component][index] * components[component][index];
					}
					const Number variance = total / static_cast<Number>(length);
					if (variance > thresholdSquared * eigenvalues[component]) {
						std::fill(components[component].begin(), components[component].end(), Number(0));
					}
				}

				// Back-project and accumulate with the cross-fade window.
				for (std::size_t index = 0; index < length; ++index) {
					const Number fade = window[std::min(index * (blockSize - 1) / std::max(length - 1, std::size_t{ 1 }),
						blockSize - 1)];
					if (fade <= Number(0)) {
						continue;
					}
					for (std::size_t channel = 0; channel < channels; ++channel) {
						Number total = Number(0);
						for (std::size_t component = 0; component < channels; ++component) {
							total += directions(channel, component) * components[component][index];
						}
						cleaned[channel][start + index] += fade * total;
					}
					weight[start + index] += fade;
				}
			}

			for (std::size_t channel = 0; channel < channels; ++channel) {
				for (std::size_t index = 0; index < samples; ++index) {
					cleaned[channel][index] = (weight[index] > Number(1e-8))
						? cleaned[channel][index] / weight[index]
						: data[channel][index];
				}
			}
			return cleaned;
		}
	};

	/// @brief  Removes the part of each channel explained by reference channels.
	///
	/// The reference set is usually EOG, but any correlated contaminant works. The
	/// regression coefficients come from the normal equations, so several references
	/// are handled at once without building a pseudo-inverse.
	///
	/// @param  data        One row of samples per channel to clean.
	/// @param  references  One row of samples per reference channel.
	/// @return The cleaned montage, same shape as @p data.
	/// @throws std::invalid_argument  When the shapes disagree or a reference is empty.
	/// @throws std::domain_error  When the reference set is collinear.
	template<typename Number = double>
	std::vector<std::vector<Number>> regressOut(const std::vector<std::vector<Number>>& data,
		const std::vector<std::vector<Number>>& references) {
		if (data.empty()) {
			throw std::invalid_argument("regressOut: the montage is empty");
		}
		if (references.empty()) {
			throw std::invalid_argument("regressOut: the reference set is empty");
		}
		const std::size_t samples = data.front().size();
		for (const auto& channel : data) {
			if (channel.size() != samples) {
				throw std::invalid_argument("regressOut: the channels differ in length");
			}
		}
		for (const auto& reference : references) {
			if (reference.size() != samples) {
				throw std::invalid_argument("regressOut: a reference differs in length");
			}
		}
		if (samples < 2) {
			throw std::invalid_argument("regressOut: at least two samples are required");
		}

		// Centre both sides so the regression does not absorb the direct-current offset.
		std::vector<Number> referenceMeans(references.size(), Number(0));
		std::vector<std::vector<Number>> centredReferences(references.size(), std::vector<Number>(samples, Number(0)));
		for (std::size_t index = 0; index < references.size(); ++index) {
			Number total = Number(0);
			for (const Number& value : references[index]) {
				total += value;
			}
			referenceMeans[index] = total / static_cast<Number>(samples);
			for (std::size_t sample = 0; sample < samples; ++sample) {
				centredReferences[index][sample] = references[index][sample] - referenceMeans[index];
			}
		}

		const std::size_t count = centredReferences.size();
		Matrix<Number> gram(count, count, Number(0));
		for (std::size_t first = 0; first < count; ++first) {
			for (std::size_t second = first; second < count; ++second) {
				Number total = Number(0);
				for (std::size_t sample = 0; sample < samples; ++sample) {
					total += centredReferences[first][sample] * centredReferences[second][sample];
				}
				gram(first, second) = total;
				gram(second, first) = total;
			}
		}
		Number trace = Number(0);
		for (std::size_t index = 0; index < count; ++index) {
			trace += gram(index, index);
		}
		if (!(trace > Number(0))) {
			throw std::domain_error("regressOut: the reference set has no variance");
		}
		// A tiny ridge keeps nearly collinear references solvable.
		const Number ridge = trace / static_cast<Number>(count) * static_cast<Number>(1e-9);
		for (std::size_t index = 0; index < count; ++index) {
			gram(index, index) += ridge;
		}

		std::vector<std::vector<Number>> result(data);
		for (std::size_t channel = 0; channel < data.size(); ++channel) {
			Number channelMean = Number(0);
			for (const Number& value : data[channel]) {
				channelMean += value;
			}
			channelMean /= static_cast<Number>(samples);

			std::vector<Number> right(count, Number(0));
			for (std::size_t reference = 0; reference < count; ++reference) {
				Number total = Number(0);
				for (std::size_t sample = 0; sample < samples; ++sample) {
					total += centredReferences[reference][sample] * (data[channel][sample] - channelMean);
				}
				right[reference] = total;
			}
			const std::vector<Number> coefficients = solve(gram, right);

			for (std::size_t sample = 0; sample < samples; ++sample) {
				Number predicted = Number(0);
				for (std::size_t reference = 0; reference < count; ++reference) {
					predicted += coefficients[reference] * centredReferences[reference][sample];
				}
				result[channel][sample] = data[channel][sample] - predicted;
			}
		}
		return result;
	}
}
