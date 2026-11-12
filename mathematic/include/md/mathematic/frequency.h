/// @file   frequency.h
/// @brief  A frequency that remembers its unit system, the named EEG bands and
///         the equal-tempered note mapping used by the teaching views.

#pragma once

#include <md/mathematic/angle.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

namespace md::mathematic {

	/// @brief  A frequency stored in hertz, convertible to angular frequency,
	///         period and equal-tempered note.
	///
	/// The value is always held in hertz, so two frequencies created through
	/// different constructors compare and combine consistently. The type exists
	/// to stop hertz, radians per second and seconds from being mixed silently:
	/// every conversion has a name.
	///
	/// @tparam Number  Floating-point type used for the value.
	template<typename Number = double>
	class Frequency {
	private:
		Number hertzValue;	///< Frequency in hertz.

	public:
		/// @brief  Constructs a zero frequency.
		constexpr Frequency() : hertzValue(Zero<Number>) {}

		/// @brief  Constructs a frequency from a value in hertz.
		/// @param  hertz  Frequency in hertz; zero is allowed, negative is not.
		constexpr explicit Frequency(const Number& hertz) : hertzValue(hertz) {}

		/// @brief  Creates a frequency measured in hertz.
		/// @param  hertz  Frequency in hertz.
		/// @return The equivalent frequency.
		static constexpr Frequency fromHertz(const Number& hertz) {
			return Frequency(hertz);
		}

		/// @brief  Creates a frequency from an angular frequency.
		/// @param  radian  Angular frequency in radians per second.
		/// @return The equivalent frequency in hertz.
		static Frequency fromRadian(const Number& radian) {
			return Frequency(radian / DoublePi<Number>);
		}

		/// @brief  Creates a frequency from a period.
		/// @param  seconds  Period in seconds.
		/// @return The reciprocal frequency; an infinite frequency for a zero
		///         period, and zero for an infinite period.
		static Frequency fromPeriod(const Number& seconds) {
			return Frequency(One<Number> / seconds);
		}

		/// @brief  Creates the frequency of an equal-tempered MIDI note.
		/// @param  note  MIDI note number, where 69 is A4 at 440 Hz.
		/// @return The corresponding frequency in hertz.
		static Frequency fromNote(const Number& note) {
			return Frequency(static_cast<Number>(440) *
				static_cast<Number>(std::pow(static_cast<Number>(2),
					(note - static_cast<Number>(69)) / static_cast<Number>(12))));
		}

		/// @brief  The stored value in hertz.
		/// @return The frequency in hertz.
		constexpr Number hertz() const {
			return hertzValue;
		}

		/// @brief  The value in radians per second.
		/// @return Two pi times the frequency in hertz.
		constexpr Number radian() const {
			return hertzValue * DoublePi<Number>;
		}

		/// @brief  The period of the frequency.
		/// @return One over the frequency, or an infinity at zero frequency.
		Number period() const {
			return One<Number> / hertzValue;
		}

		/// @brief  The equal-tempered note number of the frequency.
		/// @return The MIDI note, 69 for 440 Hz; an infinity at zero frequency.
		Number note() const {
			return static_cast<Number>(69) + static_cast<Number>(12) *
				(static_cast<Number>(std::log2(hertzValue)) - static_cast<Number>(std::log2(static_cast<Number>(440))));
		}

		/// @brief  The nearest equal-tempered note number.
		/// @return The MIDI note rounded to the nearest integer.
		long long nearestNote() const {
			const Number value = note();
			if (!std::isfinite(value)) {
				return 0;
			}
			return static_cast<long long>(std::llround(value));
		}

		/// @brief  The ASCII name of the nearest equal-tempered note.
		/// @return A name such as "A4", "C#5" or "Bb3"; "invalid" when the
		///         frequency is not positive and finite.
		std::string noteName() const {
			static const char* const names[12] = {
				"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
			};
			if (!(hertzValue > Zero<Number>) || !std::isfinite(hertzValue)) {
				return "invalid";
			}
			const long long noteValue = nearestNote();
			const long long octave = noteValue / 12 - 1;
			const long long degree = ((noteValue % 12) + 12) % 12;
			return std::string(names[degree]) + std::to_string(octave);
		}

		/// @brief  The phase a signal of this frequency accumulates in a time.
		/// @param  seconds  Elapsed time in seconds.
		/// @return Two pi times the frequency times @p seconds, as an angle.
		constexpr Angle<Number> phaseAfter(const Number& seconds) const {
			return Angle<Number>::fromRadian(radian() * seconds);
		}

		/// @brief  Reports whether the frequency is usable.
		/// @return True when the value is finite and non-negative.
		bool isValid() const {
			return std::isfinite(hertzValue) && hertzValue >= Zero<Number>;
		}

		/// @brief  Returns the frequency multiplied by a factor.
		/// @param  factor  Multiplier, typically a small integer for harmonics.
		/// @return A new frequency.
		constexpr Frequency operator*(const Number& factor) const {
			return Frequency(hertzValue * factor);
		}

		/// @brief  Returns the frequency divided by a factor.
		/// @param  divisor  Divisor.
		/// @return A new frequency.
		constexpr Frequency operator/(const Number& divisor) const {
			return Frequency(hertzValue / divisor);
		}

		/// @brief  Returns the n-th harmonic of the frequency.
		/// @param  index  Harmonic index, one for the fundamental.
		/// @return The frequency multiplied by @p index.
		constexpr Frequency harmonic(const long long index) const {
			return Frequency(hertzValue * static_cast<Number>(index));
		}

		/// @brief  Tests whether two frequencies agree to within a relative tolerance.
		/// @param  other    Frequency to compare with.
		/// @param  epsilon  Relative tolerance; defaults to eight times the machine
		///                  epsilon of @p Number.
		/// @return True when the values agree to within @p epsilon.
		bool isApproximately(const Frequency& other,
			const Number& epsilon = std::numeric_limits<Number>::epsilon() * static_cast<Number>(8)) const {
			const Number difference = std::abs(hertzValue - other.hertzValue);
			const Number scale = std::max(std::max(std::abs(hertzValue), std::abs(other.hertzValue)),
				static_cast<Number>(1));
			return difference <= epsilon * scale;
		}

		/// @brief  Equality test, using the approximate comparison above.
		bool operator==(const Frequency& other) const {
			return isApproximately(other);
		}

		/// @brief  Inequality test.
		bool operator!=(const Frequency& other) const {
			return !(*this == other);
		}

		/// @brief  Strict ordering by hertz.
		bool operator<(const Frequency& other) const {
			return hertzValue < other.hertzValue;
		}

		/// @brief  Strict ordering by hertz.
		bool operator>(const Frequency& other) const {
			return other < *this;
		}

		/// @brief  Non-strict ordering by hertz.
		bool operator<=(const Frequency& other) const {
			return !(other < *this);
		}

		/// @brief  Non-strict ordering by hertz.
		bool operator>=(const Frequency& other) const {
			return !(*this < other);
		}
	};

	/// @brief  Scalar-first multiplication, so that 2 * frequency reads naturally.
	template<typename Number, typename Scalar,
		typename = std::enable_if_t<std::is_arithmetic<Scalar>::value>>
	constexpr Frequency<Number> operator*(const Scalar& factor, const Frequency<Number>& frequency) {
		return frequency * static_cast<Number>(factor);
	}

	/// @brief  Writes a frequency to a stream in hertz.
	template<typename Number>
	std::ostream& operator<<(std::ostream& stream, const Frequency<Number>& frequency) {
		stream << frequency.hertz() << " Hz";
		return stream;
	}

	/// @brief  A half-open interval on the frequency axis, [lower, upper).
	///
	/// Bands are used to name the conventional EEG ranges and to integrate a
	/// power spectrum. The default edges follow framework section 5.5 and are
	/// meant to be overridden per protocol rather than hard-coded in the UI.
	///
	/// @tparam Number  Floating-point type used for the edges.
	template<typename Number = double>
	class FrequencyBand {
	private:
		Frequency<Number> lowerBound;	///< Inclusive lower edge.
		Frequency<Number> upperBound;	///< Exclusive upper edge.

	public:
		/// @brief  Constructs an empty band from zero to zero.
		constexpr FrequencyBand() = default;

		/// @brief  Constructs a band from two edges.
		/// @param  lower  Inclusive lower edge.
		/// @param  upper  Exclusive upper edge; may lie below @p lower, in which
		///                case the band is empty and contains no frequency.
		constexpr FrequencyBand(const Frequency<Number>& lower, const Frequency<Number>& upper)
			: lowerBound(lower), upperBound(upper) {}

		/// @brief  The delta band, 0.5-4 Hz.
		static constexpr FrequencyBand delta() {
			return FrequencyBand(Frequency<Number>::fromHertz(static_cast<Number>(0.5)),
				Frequency<Number>::fromHertz(static_cast<Number>(4)));
		}

		/// @brief  The theta band, 4-8 Hz.
		static constexpr FrequencyBand theta() {
			return FrequencyBand(Frequency<Number>::fromHertz(static_cast<Number>(4)),
				Frequency<Number>::fromHertz(static_cast<Number>(8)));
		}

		/// @brief  The alpha band, 8-13 Hz.
		static constexpr FrequencyBand alpha() {
			return FrequencyBand(Frequency<Number>::fromHertz(static_cast<Number>(8)),
				Frequency<Number>::fromHertz(static_cast<Number>(13)));
		}

		/// @brief  The beta band, 13-30 Hz.
		static constexpr FrequencyBand beta() {
			return FrequencyBand(Frequency<Number>::fromHertz(static_cast<Number>(13)),
				Frequency<Number>::fromHertz(static_cast<Number>(30)));
		}

		/// @brief  The low gamma band, 30-45 Hz.
		static constexpr FrequencyBand lowGamma() {
			return FrequencyBand(Frequency<Number>::fromHertz(static_cast<Number>(30)),
				Frequency<Number>::fromHertz(static_cast<Number>(45)));
		}

		/// @brief  The high gamma band, 45-80 Hz.
		static constexpr FrequencyBand highGamma() {
			return FrequencyBand(Frequency<Number>::fromHertz(static_cast<Number>(45)),
				Frequency<Number>::fromHertz(static_cast<Number>(80)));
		}

		/// @brief  The conventional EEG bands, in increasing frequency order.
		/// @return Delta, theta, alpha, beta, low gamma and high gamma.
		static std::vector<FrequencyBand> eegBands() {
			return { delta(), theta(), alpha(), beta(), lowGamma(), highGamma() };
		}

		/// @brief  The inclusive lower edge.
		constexpr const Frequency<Number>& lower() const {
			return lowerBound;
		}

		/// @brief  The exclusive upper edge.
		constexpr const Frequency<Number>& upper() const {
			return upperBound;
		}

		/// @brief  Width of the band in hertz.
		Number width() const {
			return upperBound.hertz() - lowerBound.hertz();
		}

		/// @brief  Geometric centre of the band.
		/// @return The square root of the product of the edges, which is the
		///         natural centre of a band that spans a fixed ratio.
		/// @note   A band that does not start at a positive frequency has an
		///         arithmetic centre instead.
		Frequency<Number> centre() const {
			const Number low = lowerBound.hertz();
			const Number high = upperBound.hertz();
			if (low > Zero<Number> && high > Zero<Number>) {
				return Frequency<Number>::fromHertz(static_cast<Number>(std::sqrt(low * high)));
			}
			return Frequency<Number>::fromHertz((low + high) / static_cast<Number>(2));
		}

		/// @brief  Tests whether a frequency falls inside the band.
		/// @param  frequency  Frequency to test.
		/// @return True when lower() <= @p frequency < upper().
		bool contains(const Frequency<Number>& frequency) const {
			return lowerBound <= frequency && frequency < upperBound;
		}

		/// @brief  Maps a fraction of the band width to a frequency.
		/// @param  fraction  Position inside the band, zero at the lower edge and
		///                   one at the upper edge.
		Frequency<Number> at(const Number& fraction) const {
			return Frequency<Number>::fromHertz(lowerBound.hertz() + width() * fraction);
		}

		/// @brief  Tests whether two bands share the same edges.
		bool operator==(const FrequencyBand& other) const {
			return lowerBound == other.lowerBound && upperBound == other.upperBound;
		}

		/// @brief  Inequality test.
		bool operator!=(const FrequencyBand& other) const {
			return !(*this == other);
		}
	};

	/// @brief  Writes a band to a stream as "[lower, upper)".
	template<typename Number>
	std::ostream& operator<<(std::ostream& stream, const FrequencyBand<Number>& band) {
		stream << '[' << band.lower() << ", " << band.upper() << ')';
		return stream;
	}
}
