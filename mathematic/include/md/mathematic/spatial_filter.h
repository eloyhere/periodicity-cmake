/// @file   spatial_filter.h
/// @brief  Common spatial patterns: filters that maximise the variance ratio
///         between two conditions, plus the log-variance features built on them.
///

#pragma once

#include <md/mathematic/decomposition.h>
#include <md/mathematic/matrix.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace md::mathematic {

	/// @brief  A fitted common spatial patterns decomposition.
	///
	/// @tparam Number  Floating-point type used for the matrices.
	template<typename Number = double>
	struct CspResult {
		Matrix<Number> filters;				///< channels x components, each column a filter.
		Matrix<Number> patterns;			///< channels x components, the matching patterns.
		std::vector<Number> eigenvalues;	///< Variance ratio of each retained component.
		std::size_t channels = 0;			///< Number of channels the filters expect.

		/// @brief  Projects one epoch onto the retained components.
		/// @param  epoch  channels x samples.
		/// @return components x samples.
		/// @throws std::invalid_argument  When the epoch has the wrong channel count.
		Matrix<Number> project(const Matrix<Number>& epoch) const {
			if (epoch.rows() != channels) {
				throw std::invalid_argument("CspResult::project: the epoch has the wrong channel count");
			}
			const std::size_t components = filters.columns();
			Matrix<Number> result(components, epoch.columns(), Number(0));
			for (std::size_t component = 0; component < components; ++component) {
				for (std::size_t row = 0; row < channels; ++row) {
					const Number weight = filters(row, component);
					if (weight == Number(0)) {
						continue;
					}
					for (std::size_t sample = 0; sample < epoch.columns(); ++sample) {
						result(component, sample) += weight * epoch(row, sample);
					}
				}
			}
			return result;
		}

		/// @brief  The normalised log-variance feature vector of one epoch.
		///
		/// Each component contributes log(variance), with the vector shifted so the
		/// entries sum to zero. That removes the overall amplitude, which varies with
		/// electrode impedance and has nothing to do with the condition.
		///
		/// @param  epoch  channels x samples.
		/// @return One feature per component.
		std::vector<Number> features(const Matrix<Number>& epoch) const {
			const Matrix<Number> projected = project(epoch);
			const std::size_t components = projected.rows();
			std::vector<Number> values(components, Number(0));

			Number total = Number(0);
			for (std::size_t component = 0; component < components; ++component) {
				Number square = Number(0);
				for (std::size_t sample = 0; sample < projected.columns(); ++sample) {
					const Number value = projected(component, sample);
					square += value * value;
				}
				const Number variance = square / static_cast<Number>(projected.columns());
				values[component] = static_cast<Number>(std::log(variance + Number(1e-30)));
				total += values[component];
			}
			const Number shift = total / static_cast<Number>(components);
			for (Number& value : values) {
				value -= shift;
			}
			return values;
		}
	};

	/// @brief  The mean normalised covariance of a group of epochs.
	/// @param  epochs  One channels x samples matrix per epoch.
	/// @return channels x channels covariance, averaged over epochs.
	/// @throws std::invalid_argument  When the group is empty or ragged.
	template<typename Number = double>
	Matrix<Number> meanCovariance(const std::vector<Matrix<Number>>& epochs) {
		if (epochs.empty()) {
			throw std::invalid_argument("meanCovariance: the group is empty");
		}
		const std::size_t channels = epochs.front().rows();
		const std::size_t samples = epochs.front().columns();
		if (channels == 0 || samples == 0) {
			throw std::invalid_argument("meanCovariance: an epoch is empty");
		}
		for (const auto& epoch : epochs) {
			if (epoch.rows() != channels || epoch.columns() != samples) {
				throw std::invalid_argument("meanCovariance: the epochs differ in shape");
			}
		}

		Matrix<Number> total(channels, channels, Number(0));
		for (const auto& epoch : epochs) {
			Matrix<Number> product(channels, channels, Number(0));
			Number trace = Number(0);
			for (std::size_t row = 0; row < channels; ++row) {
				for (std::size_t column = row; column < channels; ++column) {
					Number value = Number(0);
					for (std::size_t sample = 0; sample < samples; ++sample) {
						value += epoch(row, sample) * epoch(column, sample);
					}
					product(row, column) = value;
					product(column, row) = value;
					if (row == column) {
						trace += value;
					}
				}
			}
			if (!(trace > Number(0))) {
				continue;
			}
			for (std::size_t row = 0; row < channels; ++row) {
				for (std::size_t column = 0; column < channels; ++column) {
					total(row, column) += product(row, column) / trace;
				}
			}
		}

		const Number divisor = static_cast<Number>(epochs.size());
		for (std::size_t row = 0; row < channels; ++row) {
			for (std::size_t column = 0; column < channels; ++column) {
				total(row, column) /= divisor;
			}
		}
		return total;
	}

	/// @brief  Fits common spatial patterns to two groups of epochs.
	/// @param  first       Epochs of the first condition.
	/// @param  second      Epochs of the second condition, same shape as the first group.
	/// @param  components  Components to keep from each end of the spectrum; zero keeps all.
	/// @param  regularisation  Added to the diagonal of the composite covariance, as a
	///                         fraction of its mean diagonal; 1e-6 is a safe default.
	/// @return The filters, patterns and variance ratios, ordered by ratio.
	/// @throws std::invalid_argument  When a group is empty or the shapes disagree.
	/// @throws std::domain_error  When the composite covariance is not positive.
	/// @note   The filters are orthonormal with respect to the composite covariance, so
	///         they are comparable across subjects; the eigenvalues are the share of
	///         composite variance each filter explains for the first condition.
	template<typename Number = double>
	CspResult<Number> commonSpatialPatterns(const std::vector<Matrix<Number>>& first,
		const std::vector<Matrix<Number>>& second, const std::size_t components = 0,
		const Number& regularisation = static_cast<Number>(1e-6)) {
		if (first.empty() || second.empty()) {
			throw std::invalid_argument("commonSpatialPatterns: both conditions need epochs");
		}
		const std::size_t channels = first.front().rows();
		if (second.front().rows() != channels) {
			throw std::invalid_argument("commonSpatialPatterns: the conditions differ in channel count");
		}

		Matrix<Number> firstCovariance = meanCovariance(first);
		Matrix<Number> secondCovariance = meanCovariance(second);
		Matrix<Number> composite(channels, channels, Number(0));
		Number diagonal = Number(0);
		for (std::size_t row = 0; row < channels; ++row) {
			for (std::size_t column = 0; column < channels; ++column) {
				composite(row, column) = firstCovariance(row, column) + secondCovariance(row, column);
			}
			diagonal += composite(row, row);
		}
		diagonal /= static_cast<Number>(channels);
		if (!(diagonal > Number(0))) {
			throw std::domain_error("commonSpatialPatterns: the composite covariance is degenerate");
		}
		const Number ridge = regularisation * diagonal;
		for (std::size_t index = 0; index < channels; ++index) {
			composite(index, index) += ridge;
			firstCovariance(index, index) += ridge;
		}

		// Whitening: W^T C W = I, so the generalised problem becomes symmetric.
		const EigenDecomposition<Number> compositeSpectrum = symmetricEigen(composite);
		Matrix<Number> whitening(channels, channels, Number(0));
		for (std::size_t column = 0; column < channels; ++column) {
			if (!(compositeSpectrum.values[column] > Number(0))) {
				throw std::domain_error("commonSpatialPatterns: the composite covariance is not positive definite");
			}
			const Number scale = static_cast<Number>(1)
				/ static_cast<Number>(std::sqrt(compositeSpectrum.values[column]));
			for (std::size_t row = 0; row < channels; ++row) {
				whitening(row, column) = compositeSpectrum.vectors(row, column) * scale;
			}
		}

		Matrix<Number> whitened = multiply(multiply(transpose(whitening), firstCovariance), whitening);

		// W^T C1 W is symmetric in exact arithmetic, but two chained products leave
		// rounding asymmetry around 1e-15, which is the order the eigensolver uses as
		// its symmetry tolerance. Average the triangles so the check never trips.
		for (std::size_t row = 0; row < channels; ++row) {
			for (std::size_t column = row + 1; column < channels; ++column) {
				const Number average = (whitened(row, column) + whitened(column, row)) / Number(2);
				whitened(row, column) = average;
				whitened(column, row) = average;
			}
		}
		const EigenDecomposition<Number> spectrum = symmetricEigen(whitened);
		const std::vector<Number>& ratios = spectrum.values;
		const Matrix<Number> filters = multiply(whitening, spectrum.vectors);

		// Descending ratio, so the first column is the strongest first-condition filter.
		std::vector<std::size_t> order(channels);
		std::iota(order.begin(), order.end(), std::size_t{ 0 });
		std::sort(order.begin(), order.end(), [&ratios](const std::size_t left, const std::size_t right) {
			return ratios[left] > ratios[right];
		});

		const std::size_t perSide = (components == 0) ? channels : std::min(components, channels);
		std::vector<std::size_t> selected;
		if (perSide * 2 >= channels) {
			selected = order;
		}
		else {
			selected.insert(selected.end(), order.begin(), order.begin() + static_cast<std::ptrdiff_t>(perSide));
			selected.insert(selected.end(), order.end() - static_cast<std::ptrdiff_t>(perSide), order.end());
		}

		CspResult<Number> result;
		result.channels = channels;
		result.filters = Matrix<Number>(channels, selected.size(), Number(0));
		result.eigenvalues.resize(selected.size());
		for (std::size_t index = 0; index < selected.size(); ++index) {
			const std::size_t source = selected[index];
			result.eigenvalues[index] = ratios[source];
			for (std::size_t row = 0; row < channels; ++row) {
				result.filters(row, index) = filters(row, source);
			}
		}

		// Patterns are the columns of the inverse of the full square filter matrix:
		// they say where each component lives on the scalp, which is what a
		// topographic plot needs. Inverting the truncated matrix would be wrong and,
		// for a partial selection, not even square.
		const Matrix<Number> filterInverse = inverse(filters);
		result.patterns = Matrix<Number>(channels, selected.size(), Number(0));
		for (std::size_t component = 0; component < selected.size(); ++component) {
			for (std::size_t row = 0; row < channels; ++row) {
				result.patterns(row, component) = filterInverse(selected[component], row);
			}
		}
		return result;
	}
}
