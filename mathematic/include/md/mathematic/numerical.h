/// @file   numerical.h
/// @brief  A complex number type with arithmetic, comparison and the phase
///         relations used by the project's signal processing code.

#pragma once

#include <md/mathematic/angle.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <ostream>
#include <type_traits>

namespace md::mathematic {

	/// @brief  A complex number stored as separate real and imaginary parts.
	///
	/// Besides the usual arithmetic, the type offers the modulus/argument view
	/// of a complex number and the phase relations angular(), similarity() and
	/// tendency().
	///
	/// @tparam Number  Arithmetic type used for the real and imaginary parts.
	template<typename Number>
	class Complex {
	private:
		Number realPart;		///< Real component.
		Number imaginaryPart;	///< Imaginary component.

	public:
		/// @brief  Constructs the number 0 + 0i.
		constexpr Complex() : realPart(Zero<Number>), imaginaryPart(Zero<Number>) {}

		/// @brief  Constructs a complex number from its two components.
		/// @param  real       Real component.
		/// @param  imaginary  Imaginary component.
		constexpr Complex(Number real, Number imaginary) : realPart(real), imaginaryPart(imaginary) {}

		/// @brief  Creates a complex number from polar coordinates.
		/// @param  length  Modulus of the result, which should not be negative.
		/// @param  angle   Argument of the result.
		/// @return The number with the given modulus and argument.
		static Complex fromPolar(const Number& length, const Angle<Number>& angle) {
			return Complex(length * angle.cosine(), length * angle.sine());
		}

		/// @brief  Returns the real component.
		/// @return The real part of the number.
		constexpr Number real() const {
			return realPart;
		}

		/// @brief  Returns the imaginary component.
		/// @return The imaginary part of the number.
		constexpr Number imaginary() const {
			return imaginaryPart;
		}

		/// @brief  Returns the modulus of the number.
		/// @return The absolute value, computed without avoidable overflow.
		Number magnitude() const {
			return static_cast<Number>(std::hypot(realPart, imaginaryPart));
		}

		/// @brief  Returns the squared modulus of the number.
		/// @return The modulus squared, which avoids a square root and is
		///         therefore cheaper than magnitude().
		constexpr Number magnitudeSquared() const {
			return realPart * realPart + imaginaryPart * imaginaryPart;
		}

		/// @brief  Returns the argument, or phase, of the number.
		/// @return The angle in radians, in the interval (-pi, pi]; zero at the origin.
		Angle<Number> argument() const {
			return arctangent2(imaginaryPart, realPart);
		}

		/// @brief  Returns the complex conjugate.
		/// @return A new number with the sign of the imaginary part reversed.
		constexpr Complex conjugate() const {
			return Complex(realPart, -imaginaryPart);
		}

		/// @brief  Returns the reciprocal, 1 divided by the number.
		/// @return A new number holding the reciprocal.
		/// @warning Undefined at the origin, where the modulus is zero.
		constexpr Complex reciprocal() const {
			const Number denominator = magnitudeSquared();
			return Complex(realPart / denominator, -imaginaryPart / denominator);
		}

		/// @brief  Returns the number scaled to unit modulus.
		/// @return A new number with the same argument and a modulus of one.
		/// @warning Undefined at the origin, where the modulus is zero.
		Complex normalized() const {
			const Number length = magnitude();
			return Complex(realPart / length, imaginaryPart / length);
		}

		/// @brief  Returns the number rotated anti-clockwise about the origin.
		/// @param  angle  Angle through which to rotate.
		/// @return A new, rotated number.
		Complex rotated(const Angle<Number>& angle) const {
			const Number cosine = angle.cosine();
			const Number sine = angle.sine();
			return Complex(realPart * cosine - imaginaryPart * sine,
				realPart * sine + imaginaryPart * cosine);
		}

		/// @brief  Returns the angle through which this number must be rotated
		///         to reach another one.
		/// @param  other  Number to turn towards.
		/// @return argument(other) - argument(this), measured in radians.
		Angle<Number> angular(const Complex& other) const {
			return other.argument() - argument();
		}

		/// @brief  Returns the phase-only similarity with another number.
		/// @param  other  Number to compare with.
		/// @return The unit number exp(i * angular(other)), which depends on the
		///         phase difference alone.
		/// @note   Evaluated as the normalised conjugate product, which needs one
		///         square root instead of two arctangents and two trigonometric
		///         calls. When either operand is the origin the product vanishes
		///         and the result falls back to the polar definition, so the
		///         historical "zero has phase zero" behaviour is preserved.
		Complex similarity(const Complex& other) const {
			const Complex product = conjugate() * other;
			const Number length = product.magnitude();
			if (length == Zero<Number>) {
				const Angle<Number> difference = angular(other);
				return Complex(difference.cosine(), difference.sine());
			}
			return Complex(product.real() / length, product.imaginary() / length);
		}

		/// @brief  Returns the magnitude-weighted phase relation with another number.
		/// @param  other  Number to compare with.
		/// @return |this| times |other| times exp(i * angular(other)), which is
		///         exactly the Hermitian product conjugate(this) * other.
		/// @note   Unlike the polar formulation this needs no transcendental
		///         function at all, and it is the form used by every coherence
		///         accumulator built on the type.
		constexpr Complex tendency(const Complex& other) const {
			return conjugate() * other;
		}

		/// @brief  Alias for tendency(), named after the Hermitian product it computes.
		/// @param  other  Number to compare with.
		/// @return conjugate(this) * other.
		constexpr Complex hermitian(const Complex& other) const {
			return conjugate() * other;
		}

		/// @brief  The real part of the similarity, a cosine of the phase difference.
		/// @param  other  Number to compare with.
		/// @return cos(angular(other)), in the interval [-1, 1].
		/// @note   This is the scalar a caller usually wants when writing
		///         "if (similarity > threshold)".
		Number cosineSimilarity(const Complex& other) const {
			return similarity(other).real();
		}

		/// @brief  Tests whether the number is too small to carry a phase.
		/// @param  threshold  Smallest meaningful modulus; defaults to eight times
		///                    the machine epsilon of @p Number.
		/// @return True when the squared modulus does not exceed the squared
		///         threshold.
		/// @note   Phase estimates from a negligible number are dominated by
		///         noise; callers accumulating phase statistics should skip
		///         samples for which this returns true.
		constexpr bool isNegligible(
			const Number& threshold = std::numeric_limits<Number>::epsilon() * static_cast<Number>(8)) const {
			return magnitudeSquared() <= threshold * threshold;
		}

		/// @brief  Adds another number to this one.
		/// @param  other  Number to add.
		/// @return Reference to this number.
		constexpr Complex& operator+=(const Complex& other) {
			realPart += other.realPart;
			imaginaryPart += other.imaginaryPart;
			return *this;
		}

		/// @brief  Subtracts another number from this one.
		/// @param  other  Number to subtract.
		/// @return Reference to this number.
		constexpr Complex& operator-=(const Complex& other) {
			realPart -= other.realPart;
			imaginaryPart -= other.imaginaryPart;
			return *this;
		}

		/// @brief  Multiplies this number by another one.
		/// @param  other  Number to multiply by.
		/// @return Reference to this number.
		constexpr Complex& operator*=(const Complex& other) {
			const Number realResult = realPart * other.realPart - imaginaryPart * other.imaginaryPart;
			const Number imaginaryResult = realPart * other.imaginaryPart + imaginaryPart * other.realPart;
			realPart = realResult;
			imaginaryPart = imaginaryResult;
			return *this;
		}

		/// @brief  Divides this number by another one.
		/// @param  other  Number used as the divisor.
		/// @return Reference to this number.
		/// @warning Undefined when @p other is the origin.
		constexpr Complex& operator/=(const Complex& other) {
			const Number denominator = other.magnitudeSquared();
			const Number realResult = (realPart * other.realPart + imaginaryPart * other.imaginaryPart) / denominator;
			const Number imaginaryResult = (imaginaryPart * other.realPart - realPart * other.imaginaryPart) / denominator;
			realPart = realResult;
			imaginaryPart = imaginaryResult;
			return *this;
		}

		/// @brief  Scales this number by a real factor.
		/// @param  scale  Multiplier.
		/// @return Reference to this number.
		constexpr Complex& operator*=(const Number& scale) {
			realPart *= scale;
			imaginaryPart *= scale;
			return *this;
		}

		/// @brief  Divides this number by a real factor.
		/// @param  scale  Divisor.
		/// @return Reference to this number.
		constexpr Complex& operator/=(const Number& scale) {
			realPart /= scale;
			imaginaryPart /= scale;
			return *this;
		}

		/// @brief  Returns the sum of two numbers.
		/// @param  other  Number to add.
		/// @return A new number holding the sum.
		constexpr Complex operator+(const Complex& other) const {
			Complex result(*this);
			result += other;
			return result;
		}

		/// @brief  Returns the difference of two numbers.
		/// @param  other  Number to subtract.
		/// @return A new number holding the difference.
		constexpr Complex operator-(const Complex& other) const {
			Complex result(*this);
			result -= other;
			return result;
		}

		/// @brief  Returns the product of two numbers.
		/// @param  other  Number to multiply by.
		/// @return A new number holding the product.
		constexpr Complex operator*(const Complex& other) const {
			Complex result(*this);
			result *= other;
			return result;
		}

		/// @brief  Returns the quotient of two numbers.
		/// @param  other  Number used as the divisor.
		/// @return A new number holding the quotient.
		/// @warning Undefined when @p other is the origin.
		constexpr Complex operator/(const Complex& other) const {
			Complex result(*this);
			result /= other;
			return result;
		}

		/// @brief  Returns this number scaled by a real factor.
		/// @param  scale  Multiplier.
		/// @return A new number holding the scaled value.
		constexpr Complex operator*(const Number& scale) const {
			return Complex(realPart * scale, imaginaryPart * scale);
		}

		/// @brief  Returns this number divided by a real factor.
		/// @param  scale  Divisor.
		/// @return A new number holding the divided value.
		constexpr Complex operator/(const Number& scale) const {
			return Complex(realPart / scale, imaginaryPart / scale);
		}

		/// @brief  Returns this number with both components negated.
		/// @return A new number holding the negation.
		constexpr Complex operator-() const {
			return Complex(-realPart, -imaginaryPart);
		}

		/// @brief  Returns this number unchanged.
		/// @return A copy of this number.
		constexpr Complex operator+() const {
			return *this;
		}

		/// @brief  Tests whether two numbers agree to within a relative tolerance.
		/// @param  other    Number to compare with.
		/// @param  epsilon  Relative tolerance; defaults to eight times the machine
		///                  epsilon of @p Number.
		/// @return True when both the real and the imaginary parts agree to
		///         within @p epsilon.
		/// @note   The complex numbers admit no ordering relation, so no less-than,
		///         greater-than or non-strict ordering operators are provided.
		bool isApproximately(const Complex& other,
			const Number& epsilon = std::numeric_limits<Number>::epsilon() * static_cast<Number>(8)) const {
			const Number difference = std::max(std::abs(realPart), std::abs(imaginaryPart));
			const Number otherDifference = std::max(std::abs(other.realPart), std::abs(other.imaginaryPart));
			const Number reference = std::max(std::max(difference, otherDifference), static_cast<Number>(1));
			return std::abs(realPart - other.realPart) <= epsilon * reference
				&& std::abs(imaginaryPart - other.imaginaryPart) <= epsilon * reference;
		}

		/// @brief  Equality test, using the approximate comparison above.
		/// @param  other  Number to compare with.
		/// @return True when both components agree to within the default tolerance.
		bool operator==(const Complex& other) const {
			return isApproximately(other);
		}

		/// @brief  Inequality test.
		/// @param  other  Number to compare with.
		/// @return True when either component differs by more than the default tolerance.
		bool operator!=(const Complex& other) const {
			return !(*this == other);
		}
	};

	/// @brief  Scalar-first multiplication, so that 2 * z reads naturally.
	/// @param  scale  Multiplier.
	/// @param  value  Number to scale.
	/// @return The scaled number.
	/// @tparam Number  Arithmetic type used for the components.
	/// @tparam Scalar  Arithmetic type of the multiplier.
	template<typename Number, typename Scalar,
		typename = std::enable_if_t<std::is_arithmetic<Scalar>::value>>
	constexpr Complex<Number> operator*(const Scalar& scale, const Complex<Number>& value) {
		return value * scale;
	}

	/// @brief  Writes a complex number to a stream in the form 3 + 4i.
	/// @param  stream  Destination stream.
	/// @param  value   Number to write.
	/// @return The stream, so that insertions can be chained.
	/// @note   A negative imaginary part is printed as 3 - 4i.
	/// @tparam Number  Arithmetic type used for the components.
	template<typename Number>
	std::ostream& operator<<(std::ostream& stream, const Complex<Number>& value) {
		const Number imaginary = value.imaginary();
		stream << value.real() << (imaginary < Zero<Number> ? " - " : " + ") << std::abs(imaginary) << 'i';
		return stream;
	}

	/// @brief  The array covariance element of two observations.
	/// @param  a  Observation of the first channel.
	/// @param  b  Observation of the second channel.
	/// @return a times conjugate(b), which is the E[a * conj(b)] convention used
	///         by covariance matrices, CSP and beamforming.
	/// @note   Note the conjugate direction: the element equals
	///         conjugate(tendency(a, b)). Writing it the other way round is the
	///         classic bug described in framework section 5.7, so callers should
	///         use this function instead of composing the primitives by hand.
	template<typename Number>
	constexpr Complex<Number> covariance(const Complex<Number>& a, const Complex<Number>& b) {
		return a * b.conjugate();
	}

	/// @brief  Converts a project complex number to a standard one.
	/// @param  value  Number to convert.
	/// @return The equivalent std::complex, for interoperating with FFT libraries.
	template<typename Number>
	std::complex<Number> toStd(const Complex<Number>& value) {
		return std::complex<Number>(value.real(), value.imaginary());
	}

	/// @brief  Converts a standard complex number to a project one.
	/// @param  value  Number to convert.
	/// @return The equivalent project number.
	template<typename Number>
	Complex<Number> fromStd(const std::complex<Number>& value) {
		return Complex<Number>(value.real(), value.imag());
	}
}
