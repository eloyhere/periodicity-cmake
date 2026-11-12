/// @file   sample.h
/// @brief  Strong types for sample rates and sample positions, together with
///         the metadata that describes a sampled buffer.

#pragma once

#include <md/mathematic/frequency.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ostream>
#include <stdexcept>

namespace md::mathematic {

	/// @brief  A sample position on a discrete time axis.
	///
	/// A distinct type so that an index cannot be confused with a count, a
	/// time in seconds or a raw integer. The value is signed: offsets may be
	/// negative while iterating backwards.
	class SampleIndex {
	private:
		std::int64_t position;	///< Zero-based position.

	public:
		/// @brief  Constructs the index zero.
		constexpr SampleIndex() : position(0) {}

		/// @brief  Constructs an index from a signed integer.
		/// @param  value  Zero-based position.
		constexpr explicit SampleIndex(const std::int64_t value) : position(value) {}

		/// @brief  Creates an index from an unsigned offset.
		/// @param  offset  Zero-based position.
		/// @return The equivalent index.
		static constexpr SampleIndex fromOffset(const std::size_t offset) {
			return SampleIndex(static_cast<std::int64_t>(offset));
		}

		/// @brief  The stored position.
		/// @return The zero-based index.
		constexpr std::int64_t value() const {
			return position;
		}

		/// @brief  The position as an unsigned offset.
		/// @return The index, or zero when it is negative.
		constexpr std::size_t offset() const {
			return position < 0 ? std::size_t{ 0 } : static_cast<std::size_t>(position);
		}

		/// @brief  Reports whether the position is negative.
		constexpr bool isNegative() const {
			return position < 0;
		}

		/// @brief  Pre-increments the index.
		constexpr SampleIndex& operator++() {
			++position;
			return *this;
		}

		/// @brief  Post-increments the index.
		constexpr SampleIndex operator++(int) {
			SampleIndex previous(*this);
			++position;
			return previous;
		}

		/// @brief  Pre-decrements the index.
		constexpr SampleIndex& operator--() {
			--position;
			return *this;
		}

		/// @brief  Post-decrements the index.
		constexpr SampleIndex operator--(int) {
			SampleIndex previous(*this);
			--position;
			return previous;
		}

		/// @brief  Advances the index.
		constexpr SampleIndex& operator+=(const std::int64_t offset) {
			position += offset;
			return *this;
		}

		/// @brief  Retreats the index.
		constexpr SampleIndex& operator-=(const std::int64_t offset) {
			position -= offset;
			return *this;
		}

		/// @brief  Returns the index advanced by an offset.
		constexpr SampleIndex operator+(const std::int64_t offset) const {
			return SampleIndex(position + offset);
		}

		/// @brief  Returns the index retreated by an offset.
		constexpr SampleIndex operator-(const std::int64_t offset) const {
			return SampleIndex(position - offset);
		}

		/// @brief  Returns the distance between two indices.
		constexpr std::int64_t operator-(const SampleIndex& other) const {
			return position - other.position;
		}

		/// @brief  Equality test.
		constexpr bool operator==(const SampleIndex& other) const {
			return position == other.position;
		}

		/// @brief  Inequality test.
		constexpr bool operator!=(const SampleIndex& other) const {
			return position != other.position;
		}

		/// @brief  Strict ordering.
		constexpr bool operator<(const SampleIndex& other) const {
			return position < other.position;
		}

		/// @brief  Strict ordering.
		constexpr bool operator>(const SampleIndex& other) const {
			return other.position < position;
		}

		/// @brief  Non-strict ordering.
		constexpr bool operator<=(const SampleIndex& other) const {
			return !(other < *this);
		}

		/// @brief  Non-strict ordering.
		constexpr bool operator>=(const SampleIndex& other) const {
			return !(*this < other);
		}
	};

	/// @brief  Writes an index to a stream.
	inline std::ostream& operator<<(std::ostream& stream, const SampleIndex& index) {
		stream << index.value();
		return stream;
	}

	/// @brief  The rate at which a signal is sampled.
	///
	/// Stored as a Frequency so that the reciprocal interval and the Nyquist
	/// frequency are named quantities rather than anonymous numbers.
	///
	/// @tparam Number  Floating-point type used for the rate.
	template<typename Number = double>
	class SampleRate {
	private:
		Frequency<Number> frequencyValue;	///< Samples per second.

	public:
		/// @brief  Constructs a zero rate, which describes no signal.
		constexpr SampleRate() = default;

		/// @brief  Constructs a rate from a frequency.
		/// @param  frequency  Samples per second; should be positive.
		constexpr explicit SampleRate(const Frequency<Number>& frequency) : frequencyValue(frequency) {}

		/// @brief  Creates a rate from a value in hertz.
		/// @param  hertz  Samples per second.
		/// @return The equivalent rate.
		static constexpr SampleRate fromHertz(const Number& hertz) {
			return SampleRate(Frequency<Number>::fromHertz(hertz));
		}

		/// @brief  The rate as a frequency.
		constexpr const Frequency<Number>& frequency() const {
			return frequencyValue;
		}

		/// @brief  The rate in hertz.
		constexpr Number hertz() const {
			return frequencyValue.hertz();
		}

		/// @brief  The spacing between two samples.
		/// @return One over the rate, in seconds.
		Number interval() const {
			return frequencyValue.period();
		}

		/// @brief  The highest frequency that the rate can represent.
		/// @return Half of the rate.
		constexpr Frequency<Number> nyquist() const {
			return Frequency<Number>::fromHertz(frequencyValue.hertz() / static_cast<Number>(2));
		}

		/// @brief  Reports whether the rate is usable.
		/// @return True when the rate is positive and finite.
		bool isValid() const {
			return frequencyValue.isValid() && frequencyValue.hertz() > Zero<Number>
				&& std::isfinite(frequencyValue.hertz());
		}

		/// @brief  Equality test.
		bool operator==(const SampleRate& other) const {
			return frequencyValue == other.frequencyValue;
		}

		/// @brief  Inequality test.
		bool operator!=(const SampleRate& other) const {
			return !(*this == other);
		}
	};

	/// @brief  Writes a rate to a stream in hertz.
	template<typename Number>
	std::ostream& operator<<(std::ostream& stream, const SampleRate<Number>& rate) {
		stream << rate.hertz() << " Hz";
		return stream;
	}

	/// @brief  The metadata of a sampled buffer: its rate, its sample count and
	///         the duration the two imply.
	///
	/// The type describes a buffer, not the samples themselves; the samples live
	/// in a vector, a waveform or a ring buffer. The three fields are kept
	/// consistent by the factories, so duration() is always count() / rate().
	///
	/// @tparam Number  Floating-point type used for the duration.
	template<typename Number = double>
	class Sample {
	private:
		SampleRate<Number> sampling;	///< Samples per second.
		std::size_t sampleCount;		///< Number of samples in the buffer.

	public:
		/// @brief  Constructs an empty buffer with a zero rate.
		constexpr Sample() : sampleCount(0) {}

		/// @brief  Constructs a buffer from a rate and a count.
		/// @param  rate   Sampling rate; must be positive and finite.
		/// @param  count  Number of samples; must be positive.
		/// @throws std::invalid_argument  When the rate or the count is not usable.
		Sample(const SampleRate<Number>& rate, const std::size_t count)
			: sampling(rate), sampleCount(count) {
			if (!rate.isValid()) {
				throw std::invalid_argument("Sample: the sampling rate must be positive and finite");
			}
			if (count == 0) {
				throw std::invalid_argument("Sample: the sample count must be positive");
			}
		}

		/// @brief  Creates a buffer from a rate and a sample count.
		static Sample fromRateAndCount(const SampleRate<Number>& rate, const std::size_t count) {
			return Sample(rate, count);
		}

		/// @brief  Creates a buffer from a rate and a duration.
		/// @param  rate     Sampling rate; must be positive and finite.
		/// @param  seconds  Duration in seconds; must be non-negative.
		/// @return A buffer holding the rounded number of samples, at least one.
		/// @throws std::invalid_argument  When the rate is not usable or the
		///         duration is negative.
		static Sample fromRateAndDuration(const SampleRate<Number>& rate, const Number& seconds) {
			if (!rate.isValid()) {
				throw std::invalid_argument("Sample: the sampling rate must be positive and finite");
			}
			if (!(seconds >= Zero<Number>)) {
				throw std::invalid_argument("Sample: the duration must be non-negative");
			}
			const Number exact = seconds * rate.hertz();
			const std::size_t count = exact < static_cast<Number>(1)
				? std::size_t{ 1 }
				: static_cast<std::size_t>(std::llround(exact));
			return Sample(rate, count);
		}

		/// @brief  The sampling rate.
		constexpr const SampleRate<Number>& rate() const {
			return sampling;
		}

		/// @brief  The number of samples.
		constexpr std::size_t count() const {
			return sampleCount;
		}

		/// @brief  Reports whether the buffer holds no samples.
		constexpr bool empty() const {
			return sampleCount == 0;
		}

		/// @brief  The duration the buffer covers.
		/// @return count() divided by the rate, in seconds; zero for an empty buffer.
		Number duration() const {
			if (sampleCount == 0) {
				return Zero<Number>;
			}
			return static_cast<Number>(sampleCount) / sampling.hertz();
		}

		/// @brief  The time at which a sample was taken.
		/// @param  index  Sample position, which must be inside the buffer.
		/// @return The position divided by the rate, in seconds.
		/// @throws std::out_of_range  When @p index is outside [0, count()).
		Number timeAt(const SampleIndex& index) const {
			if (!contains(index)) {
				throw std::out_of_range("Sample: the index lies outside the buffer");
			}
			return static_cast<Number>(index.value()) / sampling.hertz();
		}

		/// @brief  The sample that covers a given time, rounded down.
		/// @param  time  Position on the time axis, in seconds.
		/// @return The sample whose interval contains @p time, or nothing when
		///         the time lies outside the buffer.
		std::optional<SampleIndex> indexAt(const Number& time) const {
			if (empty() || !(time >= Zero<Number>)) {
				return std::nullopt;
			}
			const Number position = std::floor(time * sampling.hertz());
			if (!(position < static_cast<Number>(sampleCount))) {
				return std::nullopt;
			}
			return SampleIndex(static_cast<std::int64_t>(position));
		}

		/// @brief  The sample closest to a given time.
		/// @param  time  Position on the time axis, in seconds.
		/// @return The nearest sample index, or nothing when the time falls
		///         outside the buffer by more than half a sample.
		std::optional<SampleIndex> nearestIndexAt(const Number& time) const {
			if (empty() || !(time >= Zero<Number>)) {
				return std::nullopt;
			}
			const std::int64_t position = static_cast<std::int64_t>(std::llround(time * sampling.hertz()));
			if (position < 0 || position >= static_cast<std::int64_t>(sampleCount)) {
				return std::nullopt;
			}
			return SampleIndex(position);
		}

		/// @brief  Tests whether an index lies inside the buffer.
		/// @param  index  Sample position.
		/// @return True when 0 <= @p index < count().
		bool contains(const SampleIndex& index) const {
			return index.value() >= 0 && index.value() < static_cast<std::int64_t>(sampleCount);
		}

		/// @brief  The first sample position.
		constexpr SampleIndex begin() const {
			return SampleIndex();
		}

		/// @brief  One past the last sample position.
		SampleIndex end() const {
			return SampleIndex(static_cast<std::int64_t>(sampleCount));
		}

		/// @brief  Equality test on the rate and the count.
		bool operator==(const Sample& other) const {
			return sampling == other.sampling && sampleCount == other.sampleCount;
		}

		/// @brief  Inequality test.
		bool operator!=(const Sample& other) const {
			return !(*this == other);
		}
	};

	/// @brief  Writes buffer metadata to a stream.
	template<typename Number>
	std::ostream& operator<<(std::ostream& stream, const Sample<Number>& sample) {
		stream << "sample[" << sample.count() << " @ " << sample.rate() << ", "
			<< sample.duration() << " s]";
		return stream;
	}
}
