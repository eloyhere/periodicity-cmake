/// @file   connectivity.h
/// @brief  Whole-montage connectivity matrices built from analytic signals, and
///         the two graph measures the connectivity view shows first.
///

#pragma once

#include <md/mathematic/coherence.h>
#include <md/mathematic/matrix.h>
#include <md/mathematic/numerical.h>
#include <md/mathematic/sample.h>
#include <md/mathematic/sampler.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace md::mathematic {

	/// @brief  Every pairwise connectivity measure of one montage, as C-by-C matrices.
	///
	/// All matrices share the same channel order and are symmetric about the
	/// diagonal except imaginaryCoherence, which is antisymmetric because the
	/// imaginary part of a coherency changes sign when the pair is swapped. The
	/// diagonal is the self-measurement: 1 for the phase-based indices and 0 for
	/// the imaginary ones.
	///
	/// @tparam Number  Floating-point type used for the measures.
	template<typename Number = double>
	struct ConnectivityMatrices {
		std::size_t channels = 0;		///< Number of channels.
		std::size_t samples = 0;		///< Number of samples per channel.
		Matrix<Number> phaseLocking;		///< PLV, in [0, 1].
		Matrix<Number> weightedPhaseLocking;	///< Amplitude weighted PLV, in [0, 1].
		Matrix<Number> coherence;			///< Magnitude squared coherence, in [0, 1].
		Matrix<Number> imaginaryCoherence;	///< ImCoh, in [-1, 1], antisymmetric.
		Matrix<Number> weightedPhaseLagIndex;	///< wPLI, in [0, 1].
		Matrix<Number> lagIndex;			///< PLI, in [0, 1].
	};

	/// @brief  Builds every connectivity matrix of a montage.
	/// @param  analytic  One row of analytic samples per channel; rows must be equally long.
	/// @return The matrices, in channel order.
	/// @throws std::invalid_argument  When the rows differ in length or are empty.
	/// @note   One pass over each channel pair fills six measures, because
	///         PhaseCoherence accumulates all of them from the same samples.
	template<typename Number = double>
	ConnectivityMatrices<Number> connectivity(const std::vector<std::vector<Complex<Number>>>& analytic) {
		ConnectivityMatrices<Number> result;
		if (analytic.empty()) {
			return result;
		}
		const std::size_t samples = analytic.front().size();
		for (const auto& channel : analytic) {
			if (channel.size() != samples) {
				throw std::invalid_argument("connectivity: the channels differ in length");
			}
		}
		if (samples == 0) {
			throw std::invalid_argument("connectivity: the channels are empty");
		}

		const std::size_t count = analytic.size();
		result.channels = count;
		result.samples = samples;
		result.phaseLocking = Matrix<Number>(count, count, Number(0));
		result.weightedPhaseLocking = Matrix<Number>(count, count, Number(0));
		result.coherence = Matrix<Number>(count, count, Number(0));
		result.imaginaryCoherence = Matrix<Number>(count, count, Number(0));
		result.weightedPhaseLagIndex = Matrix<Number>(count, count, Number(0));
		result.lagIndex = Matrix<Number>(count, count, Number(0));

		for (std::size_t first = 0; first < count; ++first) {
			for (std::size_t second = first; second < count; ++second) {
				PhaseCoherence<Number> accumulator;
				accumulator.add(analytic[first], analytic[second]);

				result.phaseLocking(first, second) = accumulator.phaseLockingValue();
				result.weightedPhaseLocking(first, second) = accumulator.weightedPhaseLocking();
				result.coherence(first, second) = accumulator.coherence();
				result.imaginaryCoherence(first, second) = accumulator.imaginaryCoherence();
				result.weightedPhaseLagIndex(first, second) = accumulator.weightedPhaseLagIndex();
				result.lagIndex(first, second) = accumulator.phaseLagIndex();

				if (second != first) {
					// The phase-based measures are symmetric; the imaginary ones flip sign.
					result.phaseLocking(second, first) = result.phaseLocking(first, second);
					result.weightedPhaseLocking(second, first) = result.weightedPhaseLocking(first, second);
					result.coherence(second, first) = result.coherence(first, second);
					result.weightedPhaseLagIndex(second, first) = result.weightedPhaseLagIndex(first, second);
					result.lagIndex(second, first) = result.lagIndex(first, second);
					result.imaginaryCoherence(second, first) = -result.imaginaryCoherence(first, second);
				}
			}
		}
		return result;
	}

	/// @brief  Builds the connectivity matrices of a real montage.
	/// @param  channels      One row of real samples per channel.
	/// @param  sampleRate    Sampling rate in hertz.
	/// @param  lineFrequency Accepted for symmetry with the rest of the pipeline;
	///                       the caller is expected to have filtered the signal.
	/// @return The matrices, computed from the analytic signals.
	/// @throws std::invalid_argument  When the montage is empty or ragged.
	/// @note   The phase branch requires narrow-band signals, so band-pass first;
	///         a broadband signal has no meaningful instantaneous phase.
	template<typename Number = double>
	ConnectivityMatrices<Number> connectivityFromReal(const std::vector<std::vector<Number>>& channels,
		const SampleRate<Number>& sampleRate) {
		if (!sampleRate.isValid()) {
			throw std::invalid_argument("connectivityFromReal: the sample rate must be positive");
		}
		if (channels.empty()) {
			return ConnectivityMatrices<Number>{};
		}
		const std::size_t samples = channels.front().size();
		for (const auto& channel : channels) {
			if (channel.size() != samples) {
				throw std::invalid_argument("connectivityFromReal: the channels differ in length");
			}
		}

		std::vector<std::vector<Complex<Number>>> analytic;
		analytic.reserve(channels.size());
		for (const auto& channel : channels) {
			analytic.push_back(Sampler<Number>::analyticSignal(channel));
		}
		return connectivity(analytic);
	}

	/// @brief  The weighted degree of one node, the sum of its off-diagonal weights.
	/// @throws std::out_of_range  When the node index is outside the matrix.
	template<typename Number = double>
	Number nodeStrength(const Matrix<Number>& weights, const std::size_t node) {
		if (!weights.isSquare() || node >= weights.rows()) {
			throw std::out_of_range("nodeStrength: the node index is outside the matrix");
		}
		Number total = Number(0);
		for (std::size_t other = 0; other < weights.rows(); ++other) {
			if (other != node) {
				total += weights(node, other);
			}
		}
		return total;
	}

	/// @brief  The binary clustering coefficient of one node.
	/// @param  weights    Symmetric weight matrix, typically PLV or wPLI.
	/// @param  node       Node to measure.
	/// @param  threshold  Weights strictly above this value count as edges.
	/// @return The share of neighbour pairs that are themselves connected; zero
	///         when the node has fewer than two neighbours.
	/// @throws std::out_of_range  When the node index is outside the matrix.
	template<typename Number = double>
	Number clusteringCoefficient(const Matrix<Number>& weights, const std::size_t node,
		const Number& threshold) {
		if (!weights.isSquare() || node >= weights.rows()) {
			throw std::out_of_range("clusteringCoefficient: the node index is outside the matrix");
		}
		std::vector<std::size_t> neighbours;
		for (std::size_t other = 0; other < weights.rows(); ++other) {
			if (other != node && weights(node, other) > threshold) {
				neighbours.push_back(other);
			}
		}
		if (neighbours.size() < 2) {
			return Number(0);
		}

		std::size_t links = 0;
		for (std::size_t first = 0; first < neighbours.size(); ++first) {
			for (std::size_t second = first + 1; second < neighbours.size(); ++second) {
				if (weights(neighbours[first], neighbours[second]) > threshold) {
					++links;
				}
			}
		}
		const Number possible = static_cast<Number>(neighbours.size() * (neighbours.size() - 1) / 2);
		return static_cast<Number>(links) / possible;
	}

	/// @brief  The mean clustering coefficient over the whole montage.
	template<typename Number = double>
	Number meanClustering(const Matrix<Number>& weights, const Number& threshold) {
		if (!weights.isSquare() || weights.rows() == 0) {
			return Number(0);
		}
		Number total = Number(0);
		for (std::size_t node = 0; node < weights.rows(); ++node) {
			total += clusteringCoefficient(weights, node, threshold);
		}
		return total / static_cast<Number>(weights.rows());
	}
}
