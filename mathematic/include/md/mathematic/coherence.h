/// @file   coherence.h
/// @brief  A single-pass accumulator that turns pairs of analytic samples into
///         the phase-coupling measures used by the connectivity view.

#pragma once

#include <md/mathematic/angle.h>
#include <md/mathematic/numerical.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace md::mathematic {

	/// @brief  Accumulates the phase and product sums needed by the coherence family.
	///
	/// One add() call feeds every measure, so a caller traverses the channel pair
	/// once instead of once per metric. The accumulator is the only place that
	/// decides which conjugate direction is used, which removes the classic
	/// mistake of writing the covariance the wrong way round.
	///
	/// All measures return zero when no sample has been added.
	///
	/// @tparam Number  Floating-point type used for the samples.
	template<typename Number = double>
	class PhaseCoherence {
	private:
		std::size_t sampleCount = 0;		///< Number of pairs accumulated.
		Complex<Number> phaseSum;			///< Sum of similarity(a, b).
		Complex<Number> productSum;			///< Sum of tendency(a, b).
		Number productWeight = Zero<Number>;	///< Sum of |tendency|, the wPLV denominator.
		Number imaginaryWeight = Zero<Number>;	///< Sum of |Im tendency|, the wPLI denominator.
		Number signSum = Zero<Number>;		///< Sum of the signs of Im tendency.
		Number firstPower = Zero<Number>;	///< Sum of |a|^2.
		Number secondPower = Zero<Number>;	///< Sum of |b|^2.

	public:
		/// @brief  Adds one pair of analytic samples.
		/// @param  first   Sample of the first channel.
		/// @param  second  Sample of the second channel.
		void add(const Complex<Number>& first, const Complex<Number>& second) {
			phaseSum += first.similarity(second);

			const Complex<Number> product = first.tendency(second);
			productSum += product;
			productWeight += product.magnitude();

			const Number imaginary = product.imaginary();
			imaginaryWeight += std::abs(imaginary);
			signSum += static_cast<Number>((imaginary > Zero<Number>) - (imaginary < Zero<Number>));

			firstPower += first.magnitudeSquared();
			secondPower += second.magnitudeSquared();
			++sampleCount;
		}

		/// @brief  Adds two equally long sample sequences.
		/// @param  first   Samples of the first channel.
		/// @param  second  Samples of the second channel.
		/// @note   Stops at the shorter sequence; no exception is thrown.
		void add(const std::vector<Complex<Number>>& first, const std::vector<Complex<Number>>& second) {
			const std::size_t count = std::min(first.size(), second.size());
			for (std::size_t index = 0; index < count; ++index) {
				add(first[index], second[index]);
			}
		}

		/// @brief  Number of accumulated pairs.
		std::size_t count() const {
			return sampleCount;
		}

		/// @brief  Reports whether no pair has been added.
		bool empty() const {
			return sampleCount == 0;
		}

		/// @brief  Clears every accumulated sum but keeps the object usable.
		void reset() {
			*this = PhaseCoherence();
		}

		/// @brief  The phase locking value.
		/// @return |mean of similarity|, in [0, 1].
		Number phaseLockingValue() const {
			if (sampleCount == 0) {
				return Zero<Number>;
			}
			return phaseSum.magnitude() / static_cast<Number>(sampleCount);
		}

		/// @brief  The amplitude-weighted phase locking value.
		/// @return |sum of tendency| divided by the sum of |tendency|.
		Number weightedPhaseLocking() const {
			if (productWeight == Zero<Number>) {
				return Zero<Number>;
			}
			return productSum.magnitude() / productWeight;
		}

		/// @brief  The magnitude-squared coherence.
		/// @return |sum of tendency|^2 divided by the product of the powers.
		Number coherence() const {
			const Number denominator = firstPower * secondPower;
			if (denominator == Zero<Number>) {
				return Zero<Number>;
			}
			return productSum.magnitudeSquared() / denominator;
		}

		/// @brief  The imaginary part of the coherency.
		/// @return Im(sum of tendency) divided by the square root of the power product.
		/// @note   Close to zero for volume conduction, which is the whole point of
		///         the measure.
		Number imaginaryCoherence() const {
			const Number denominator = firstPower * secondPower;
			if (denominator == Zero<Number>) {
				return Zero<Number>;
			}
			return productSum.imaginary() / static_cast<Number>(std::sqrt(denominator));
		}

		/// @brief  The weighted phase lag index.
		/// @return |Im(sum of tendency)| divided by the sum of |Im tendency|.
		Number weightedPhaseLagIndex() const {
			if (imaginaryWeight == Zero<Number>) {
				return Zero<Number>;
			}
			return std::abs(productSum.imaginary()) / imaginaryWeight;
		}

		/// @brief  The phase lag index.
		/// @return |mean sign of Im tendency|, in [0, 1].
		Number phaseLagIndex() const {
			if (sampleCount == 0) {
				return Zero<Number>;
			}
			return std::abs(signSum) / static_cast<Number>(sampleCount);
		}

		/// @brief  The mean phase difference.
		/// @return The argument of the phase sum; zero when nothing was added.
		Angle<Number> meanPhase() const {
			return phaseSum.argument();
		}

		/// @brief  The accumulated power of the first channel.
		Number firstPowerSum() const {
			return firstPower;
		}

		/// @brief  The accumulated power of the second channel.
		Number secondPowerSum() const {
			return secondPower;
		}
	};
}
