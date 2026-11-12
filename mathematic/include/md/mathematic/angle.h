/// @file   angle.h
/// @brief  An angle type that remembers its unit system, together with the
///         inverse trigonometric helpers that produce it.

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <ostream>
#include <type_traits>

namespace md::mathematic {

	/// @brief  Zero cast to the requested number type.
	/// @tparam Number  Arithmetic type used for angle values.
	template<typename Number>
	constexpr Number Zero = static_cast<Number>(0);

	/// @brief  One cast to the requested number type.
	/// @tparam Number  Arithmetic type used for angle values.
	template<typename Number>
	constexpr Number One = static_cast<Number>(1);

	/// @brief  Pi cast to the requested number type.
	/// @note   An integral @p Number truncates the constant to 3, so angles
	///         should normally be stored in a floating-point type.
	/// @tparam Number  Arithmetic type used for angle values.
	template<typename Number>
	constexpr Number Pi = static_cast<Number>(3.14159265358979323846264338327950288);

	/// @brief  A full turn, two pi, cast to the requested number type.
	/// @tparam Number  Arithmetic type used for angle values.
	template<typename Number>
	constexpr Number DoublePi = Pi<Number> * static_cast<Number>(2);

	/// @brief  A right angle, half of pi, cast to the requested number type.
	/// @tparam Number  Arithmetic type used for angle values.
	template<typename Number>
	constexpr Number HalfPi = Pi<Number> / static_cast<Number>(2);

	/// @brief  Forward declaration, so that the helpers below can name Angle.
	/// @tparam Number  Arithmetic type used for angle values.
	template<typename Number>
	class Angle;

	/// @brief  Arcsine of a value, returned as an angle measured in radians.
	/// @param  n  Value in the interval [-1, 1].
	/// @return The angle whose sine is @p n, in radians.
	/// @tparam Number  Arithmetic type used for the calculation.
	template<typename Number>
	Angle<Number> arcsine(const Number& n);

	/// @brief  Arccosine of a value, returned as an angle measured in radians.
	/// @param  n  Value in the interval [-1, 1].
	/// @return The angle whose cosine is @p n, in radians.
	/// @tparam Number  Arithmetic type used for the calculation.
	template<typename Number>
	Angle<Number> arccosine(const Number& n);

	/// @brief  Arctangent of a value, returned as an angle measured in radians.
	/// @param  n  Value whose arctangent is required.
	/// @return The angle whose tangent is @p n, in radians.
	/// @tparam Number  Arithmetic type used for the calculation.
	template<typename Number>
	Angle<Number> arctangent(const Number& n);

	/// @brief  Single-argument compatibility overload, equivalent to arctangent().
	/// @param  n  Value whose arctangent is required.
	/// @return The angle whose tangent is @p n, in radians.
	/// @tparam Number  Arithmetic type used for the calculation.
	template<typename Number>
	Angle<Number> arctangent2(const Number& n);

	/// @brief  Two-argument arctangent, the usual atan2(y, x), in radians.
	/// @param  y  Ordinate of the point.
	/// @param  x  Abscissa of the point.
	/// @return The argument of the point (x, y), in the interval (-pi, pi].
	/// @tparam Number  Arithmetic type used for the calculation.
	template<typename Number>
	Angle<Number> arctangent2(const Number& y, const Number& x);

	/// @brief  An angle that carries its own unit system.
	///
	/// The unit system is stored as the value that pi takes in that system:
	/// 180 for degrees and pi for radians. That single number is enough to
	/// convert between systems and to add, subtract or compare angles that were
	/// created in different units.
	///
	/// @tparam Number  Arithmetic type used to store the angle.
	template<typename Number>
	class Angle {
	private:
		Number unit;		///< Value of pi in this angle's unit system: 180 for degrees, pi for radians.
		Number magnitude;	///< Angle size measured in this unit system.

		/// @brief  Converts another angle into this angle's unit system.
		/// @param  other  Angle to convert.
		/// @return The size of @p other expressed in this angle's units.
		constexpr Number converted(const Angle& other) const {
			return unit == other.unit ? other.magnitude : other.magnitude * unit / other.unit;
		}

	public:
		/// @brief  Value of pi in the degree system.
		static constexpr Number degreeUnit() { return static_cast<Number>(180); }

		/// @brief  Value of pi in the radian system.
		static constexpr Number radianUnit() { return Pi<Number>; }

		/// @brief  Constructs a zero angle measured in radians.
		constexpr Angle() : unit(radianUnit()), magnitude(Zero<Number>) {}

		/// @brief  Constructs an angle from a size measured in radians.
		/// @param  angleValue  Angle size in radians.
		constexpr Angle(Number angleValue) : unit(radianUnit()), magnitude(angleValue) {}

		/// @brief  Constructs an angle in an explicit unit system.
		/// @param  angleUnit   Value of pi in the chosen system, 180 for degrees.
		/// @param  angleValue  Angle size in that system.
		/// @note   A zero unit is treated as radians rather than dividing by zero.
		constexpr Angle(Number angleUnit, Number angleValue)
			: unit(angleUnit == Zero<Number> ? radianUnit() : angleUnit), magnitude(angleValue) {}

		/// @brief  Creates an angle measured in degrees.
		/// @param  angleValue  Angle size in degrees.
		/// @return The equivalent angle.
		static constexpr Angle fromDegree(const Number& angleValue) {
			return Angle(degreeUnit(), angleValue);
		}

		/// @brief  Creates an angle measured in radians.
		/// @param  angleValue  Angle size in radians.
		/// @return The equivalent angle.
		static constexpr Angle fromRadian(const Number& angleValue) {
			return Angle(radianUnit(), angleValue);
		}

		/// @brief  Tests whether the angle is measured in degrees.
		/// @return True when the unit system is degrees.
		constexpr bool isDegree() const {
			return unit == degreeUnit();
		}

		/// @brief  Tests whether the angle is measured in radians.
		/// @return True when the unit system is radians.
		constexpr bool isRadian() const {
			return unit == radianUnit();
		}

		/// @brief  Numeric size of the angle in radians.
		/// @return The angle converted to radians.
		/// @note   The object itself is left unchanged.
		constexpr Number radianValue() const {
			return unit == radianUnit() ? magnitude : magnitude * Pi<Number> / unit;
		}

		/// @brief  Numeric size of the angle in degrees.
		/// @return The angle converted to degrees.
		/// @note   The object itself is left unchanged.
		constexpr Number degreeValue() const {
			return unit == degreeUnit() ? magnitude : magnitude * degreeUnit() / unit;
		}

		/// @brief  Returns the same angle expressed in degrees.
		/// @return A new angle measured in degrees.
		constexpr Angle toDegree() const {
			return Angle(degreeUnit(), degreeValue());
		}

		/// @brief  Returns the same angle expressed in radians.
		/// @return A new angle measured in radians.
		constexpr Angle toRadian() const {
			return Angle(radianUnit(), radianValue());
		}

		/// @brief  Returns the equivalent angle normalised to the interval [0, 2pi).
		/// @return A new angle with the same direction and a non-negative size.
		Angle normalized() const {
			const Number fullTurn = unit * static_cast<Number>(2);
			Number result = static_cast<Number>(std::fmod(magnitude, fullTurn));
			if (result < Zero<Number>) {
				result += fullTurn;
			}
			return Angle(unit, result);
		}

		/// @brief  Returns the equivalent angle wrapped to the interval (-pi, pi].
		/// @return A new angle with the same direction and the smallest magnitude.
		Angle wrapped() const {
			const Number fullTurn = unit * static_cast<Number>(2);
			return Angle(unit, static_cast<Number>(std::remainder(magnitude, fullTurn)));
		}

		/// @brief  Raw size held by the object, in its own unit system.
		/// @return The stored value, allowing the angle to be read like a number.
		constexpr Number operator()() const {
			return magnitude;
		}

		/// @brief  Raw size held by the object, in its own unit system.
		/// @return The stored value.
		constexpr Number value() const {
			return magnitude;
		}

		/// @brief  Value of pi in the object's unit system.
		/// @return 180 for degrees, pi for radians.
		constexpr Number unitValue() const {
			return unit;
		}

		/// @brief  Sine of the angle.
		/// @return The sine, evaluated in radians whatever the stored unit.
		Number sine() const {
			return static_cast<Number>(std::sin(radianValue()));
		}

		/// @brief  Cosine of the angle.
		/// @return The cosine, evaluated in radians whatever the stored unit.
		Number cosine() const {
			return static_cast<Number>(std::cos(radianValue()));
		}

		/// @brief  Tangent of the angle.
		/// @return The tangent, evaluated in radians whatever the stored unit.
		Number tangent() const {
			return static_cast<Number>(std::tan(radianValue()));
		}

		/// @brief  Cotangent of the angle, the cosine divided by the sine.
		/// @return The cotangent of the angle.
		/// @warning Undefined where the sine is zero.
		Number cotangent() const {
			return cosine() / sine();
		}

		/// @brief  Secant of the angle, the reciprocal of the cosine.
		/// @return The secant of the angle.
		/// @warning Undefined where the cosine is zero.
		Number secant() const {
			return static_cast<Number>(1) / cosine();
		}

		/// @brief  Cosecant of the angle, the reciprocal of the sine.
		/// @return The cosecant of the angle.
		/// @warning Undefined where the sine is zero.
		Number cosecant() const {
			return static_cast<Number>(1) / sine();
		}

		/// @brief  Hyperbolic sine of the angle.
		/// @return The hyperbolic sine, evaluated in radians whatever the stored unit.
		Number hyperbolicSine() const {
			return static_cast<Number>(std::sinh(radianValue()));
		}

		/// @brief  Hyperbolic cosine of the angle.
		/// @return The hyperbolic cosine, evaluated in radians whatever the stored unit.
		Number hyperbolicCosine() const {
			return static_cast<Number>(std::cosh(radianValue()));
		}

		/// @brief  Hyperbolic tangent of the angle.
		/// @return The hyperbolic tangent, evaluated in radians whatever the stored unit.
		Number hyperbolicTangent() const {
			return static_cast<Number>(std::tanh(radianValue()));
		}

		/// @brief  Adds another angle, converting it into this unit system first.
		/// @param  other  Angle to add.
		/// @return Reference to this angle.
		constexpr Angle& operator+=(const Angle& other) {
			magnitude += converted(other);
			return *this;
		}

		/// @brief  Subtracts another angle, converting it into this unit system first.
		/// @param  other  Angle to subtract.
		/// @return Reference to this angle.
		constexpr Angle& operator-=(const Angle& other) {
			magnitude -= converted(other);
			return *this;
		}

		/// @brief  Scales the angle by a factor.
		/// @param  scale  Multiplier.
		/// @return Reference to this angle.
		constexpr Angle& operator*=(const Number& scale) {
			magnitude *= scale;
			return *this;
		}

		/// @brief  Divides the angle by a factor.
		/// @param  scale  Divisor.
		/// @return Reference to this angle.
		constexpr Angle& operator/=(const Number& scale) {
			magnitude /= scale;
			return *this;
		}

		/// @brief  Returns the sum of two angles, in the left operand's units.
		/// @param  other  Angle to add.
		/// @return A new angle holding the sum.
		constexpr Angle operator+(const Angle& other) const {
			Angle result(*this);
			result += other;
			return result;
		}

		/// @brief  Returns the difference of two angles, in the left operand's units.
		/// @param  other  Angle to subtract.
		/// @return A new angle holding the difference.
		constexpr Angle operator-(const Angle& other) const {
			Angle result(*this);
			result -= other;
			return result;
		}

		/// @brief  Returns the angle scaled by a factor.
		/// @param  scale  Multiplier.
		/// @return A new angle of the same unit system.
		constexpr Angle operator*(const Number& scale) const {
			return Angle(unit, magnitude * scale);
		}

		/// @brief  Returns the angle divided by a factor.
		/// @param  scale  Divisor.
		/// @return A new angle of the same unit system.
		constexpr Angle operator/(const Number& scale) const {
			return Angle(unit, magnitude / scale);
		}

		/// @brief  Returns the ratio of two angles as a dimensionless number.
		/// @param  other  Angle used as the divisor.
		/// @return The size of this angle divided by the size of @p other.
		constexpr Number operator/(const Angle& other) const {
			return magnitude / converted(other);
		}

		/// @brief  Returns the angle with its sign reversed.
		/// @return A new angle of the same unit system.
		constexpr Angle operator-() const {
			return Angle(unit, -magnitude);
		}

		/// @brief  Returns the angle unchanged.
		/// @return A copy of this angle.
		constexpr Angle operator+() const {
			return *this;
		}

		/// @brief  Tests whether two angles agree to within a relative tolerance.
		/// @param  other    Angle to compare with.
		/// @param  epsilon  Relative tolerance; defaults to eight times the machine
		///                  epsilon of @p Number.
		/// @return True when both angles describe the same rotation to within @p epsilon.
		/// @note   The comparison is carried out in radians, so angles held in
		///         different unit systems are compared consistently.
		bool isApproximately(const Angle& other,
			const Number& epsilon = std::numeric_limits<Number>::epsilon() * static_cast<Number>(8)) const {
			const Number left = radianValue();
			const Number right = other.radianValue();
			const Number difference = std::abs(left - right);
			const Number scale = std::max(std::max(std::abs(left), std::abs(right)), static_cast<Number>(1));
			return difference <= epsilon * scale;
		}

		/// @brief  Equality test, using the approximate comparison above.
		/// @param  other  Angle to compare with.
		/// @return True when the angles agree to within the default tolerance.
		bool operator==(const Angle& other) const {
			return isApproximately(other);
		}

		/// @brief  Inequality test.
		/// @param  other  Angle to compare with.
		/// @return True when the angles differ by more than the default tolerance.
		bool operator!=(const Angle& other) const {
			return !(*this == other);
		}

		/// @brief  Strict ordering of two angles, carried out in radians.
		/// @param  other  Angle to compare with.
		/// @return True when this angle is strictly smaller than @p other.
		bool operator<(const Angle& other) const {
			return radianValue() < other.radianValue();
		}

		/// @brief  Strict ordering of two angles, carried out in radians.
		/// @param  other  Angle to compare with.
		/// @return True when this angle is strictly greater than @p other.
		bool operator>(const Angle& other) const {
			return other < *this;
		}

		/// @brief  Non-strict ordering of two angles.
		/// @param  other  Angle to compare with.
		/// @return True when this angle is smaller than @p other, or agrees with it.
		bool operator<=(const Angle& other) const {
			return (*this < other) || (*this == other);
		}

		/// @brief  Non-strict ordering of two angles.
		/// @param  other  Angle to compare with.
		/// @return True when this angle is greater than @p other, or agrees with it.
		bool operator>=(const Angle& other) const {
			return (*this > other) || (*this == other);
		}
	};

	/// @brief  Scalar-first multiplication, so that 2 * angle reads naturally.
	/// @param  scale  Multiplier.
	/// @param  angle  Angle to scale.
	/// @return The scaled angle.
	/// @tparam Number  Arithmetic type used to store the angle.
	/// @tparam Scalar  Arithmetic type of the multiplier.
	template<typename Number, typename Scalar,
		typename = std::enable_if_t<std::is_arithmetic<Scalar>::value>>
	constexpr Angle<Number> operator*(const Scalar& scale, const Angle<Number>& angle) {
		return angle * scale;
	}

	/// @brief  Writes an angle to a stream, tagged with its unit system.
	/// @param  stream  Destination stream.
	/// @param  angle   Angle to write.
	/// @return The stream, so that insertions can be chained.
	/// @note   Degrees print as "90 deg" and radians as "1.5708 rad".
	/// @tparam Number  Arithmetic type used to store the angle.
	template<typename Number>
	std::ostream& operator<<(std::ostream& stream, const Angle<Number>& angle) {
		stream << angle.value();
		if (angle.isDegree()) {
			stream << " deg";
		}
		else if (angle.isRadian()) {
			stream << " rad";
		}
		return stream;
	}

	/// @brief  Arcsine of a value, returned as an angle measured in radians.
	/// @param  n  Value in the interval [-1, 1].
	/// @return The angle whose sine is @p n, in radians.
	/// @tparam Number  Arithmetic type used for the calculation.
	template<typename Number>
	Angle<Number> arcsine(const Number& n) {
		return Angle<Number>::fromRadian(static_cast<Number>(std::asin(n)));
	}

	/// @brief  Arccosine of a value, returned as an angle measured in radians.
	/// @param  n  Value in the interval [-1, 1].
	/// @return The angle whose cosine is @p n, in radians.
	/// @tparam Number  Arithmetic type used for the calculation.
	template<typename Number>
	Angle<Number> arccosine(const Number& n) {
		return Angle<Number>::fromRadian(static_cast<Number>(std::acos(n)));
	}

	/// @brief  Arctangent of a value, returned as an angle measured in radians.
	/// @param  n  Value whose arctangent is required.
	/// @return The angle whose tangent is @p n, in radians.
	/// @tparam Number  Arithmetic type used for the calculation.
	template<typename Number>
	Angle<Number> arctangent(const Number& n) {
		return Angle<Number>::fromRadian(static_cast<Number>(std::atan(n)));
	}

	/// @brief  Single-argument compatibility overload, equivalent to arctangent().
	/// @param  n  Value whose arctangent is required.
	/// @return The angle whose tangent is @p n, in radians.
	/// @tparam Number  Arithmetic type used for the calculation.
	template<typename Number>
	Angle<Number> arctangent2(const Number& n) {
		return arctangent(n);
	}

	/// @brief  Two-argument arctangent, the usual atan2(y, x), in radians.
	/// @param  y  Ordinate of the point.
	/// @param  x  Abscissa of the point.
	/// @return The argument of the point (x, y), in the interval (-pi, pi].
	/// @tparam Number  Arithmetic type used for the calculation.
	template<typename Number>
	Angle<Number> arctangent2(const Number& y, const Number& x) {
		return Angle<Number>::fromRadian(static_cast<Number>(std::atan2(y, x)));
	}
}
