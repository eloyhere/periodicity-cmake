/// @file   envelope.h
/// @brief  A piecewise-linear amplitude envelope, together with the ADSR and
///         sampled-sequence constructors, interpolation and comparison helpers.

#pragma once

#include <md/mathematic/angle.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <ostream>
#include <type_traits>
#include <utility>
#include <vector>

namespace md::mathematic {

	/// @brief  A piecewise-linear amplitude envelope over a time axis.
	///
	/// The curve is stored as control points sorted by strictly increasing time.
	/// Between two neighbouring points the level varies linearly; before the
	/// first and after the last point the terminal level is held, so the
	/// envelope is defined for every time. An envelope without control points is
	/// silent: it evaluates to zero everywhere.
	///
	/// The same curve can be described in three ways: explicit control points,
	/// a finite ADSR shape (see adsr()) or a uniformly sampled amplitude
	/// sequence (see fromSamples()). Every description is stored as control
	/// points, so evaluation, arithmetic and comparison behave identically.
	///
	/// The time axis carries no unit of its own. Callers may measure it in
	/// seconds, in samples or on a normalised interval, provided that one
	/// envelope is used consistently. Envelopes naturally describe taper
	/// windows, attack and decay shapes, and the smoothed magnitude of an
	/// analytic signal.
	///
	/// @tparam Number  Arithmetic type used for times and levels.
	/// @note   An integral @p Number truncates every interpolated level, so
	///         envelopes should normally be stored in a floating-point type.
	template<typename Number>
	class Envelope {
	public:
		/// @brief  A control point that pins the envelope to a level at a time.
		struct ControlPoint {
			Number time;	///< Position on the time axis.
			Number level;	///< Envelope level at that position.
		};

	private:
		std::vector<ControlPoint> controlPoints;	///< Control points, sorted by strictly increasing time.

		/// @brief  Sorts points by time and merges points that share a time.
		/// @param  source  Points in any order.
		/// @return The points in increasing time order; where several points
		///         share a time the later one of @p source wins.
		static std::vector<ControlPoint> ordered(std::vector<ControlPoint> source) {
			std::stable_sort(source.begin(), source.end(),
				[](const ControlPoint& left, const ControlPoint& right) {
					return left.time < right.time;
				});

			std::vector<ControlPoint> result;
			result.reserve(source.size());
			for (const ControlPoint& point : source) {
				if (!result.empty() && result.back().time == point.time) {
					result.back().level = point.level;
				}
				else {
					result.push_back(point);
				}
			}
			return result;
		}

		/// @brief  Compares two levels with a relative tolerance.
		/// @param  left     First level.
		/// @param  right    Second level.
		/// @param  epsilon  Relative tolerance.
		/// @return True when the levels agree to within @p epsilon.
		static bool approximatelyEqual(const Number& left, const Number& right, const Number& epsilon) {
			const Number difference = std::abs(left - right);
			const Number scale = std::max(std::max(std::abs(left), std::abs(right)), static_cast<Number>(1));
			return difference <= epsilon * scale;
		}

	public:
		/// @brief  Constructs an empty, silent envelope.
		Envelope() = default;

		/// @brief  Constructs an envelope from a brace-enclosed point list.
		/// @param  source  Control points in any order; repeated times keep the
		///                 later point.
		Envelope(std::initializer_list<ControlPoint> source)
			: controlPoints(ordered(std::vector<ControlPoint>(source))) {}

		/// @brief  Constructs an envelope from a vector of control points.
		/// @param  source  Control points in any order; repeated times keep the
		///                 later point. The vector is consumed.
		explicit Envelope(std::vector<ControlPoint> source)
			: controlPoints(ordered(std::move(source))) {}

		/// @brief  Creates an envelope holding one level at every time.
		/// @param  level  Level to hold.
		/// @return A constant envelope with a single control point at time zero.
		static Envelope constant(const Number& level) {
			return Envelope{ControlPoint{Zero<Number>, level}};
		}

		/// @brief  Creates a linear ramp between two control points.
		/// @param  startTime   Time of the first control point.
		/// @param  startLevel  Level at @p startTime.
		/// @param  endTime     Time of the second control point.
		/// @param  endLevel    Level at @p endTime.
		/// @return An envelope holding @p startLevel before @p startTime, ramping
		///         linearly between the two points, and holding @p endLevel
		///         afterwards.
		/// @note   When the two times agree the later point wins, so the result
		///         is the constant envelope @p endLevel.
		static Envelope ramp(const Number& startTime, const Number& startLevel,
			const Number& endTime, const Number& endLevel) {
			return Envelope{ControlPoint{startTime, startLevel}, ControlPoint{endTime, endLevel}};
		}

		/// @brief  Creates a finite ADSR envelope.
		///
		/// The shape starts at zero time, rises linearly to @p peakLevel during
		/// the attack, falls linearly to @p sustainLevel during the decay, holds
		/// that level for @p holdTime, and falls linearly to zero during the
		/// release. The resulting envelope is silent before time zero and after
		/// the release.
		/// @param  attackTime    Duration of the rising attack segment.
		/// @param  decayTime     Duration of the falling decay segment.
		/// @param  sustainLevel  Level held during the sustain plateau.
		/// @param  holdTime      Duration of the sustain plateau.
		/// @param  releaseTime   Duration of the final falling release segment.
		/// @param  peakLevel     Level reached at the end of the attack; defaults
		///                       to one.
		/// @return An envelope beginning at time zero.
		/// @note   Negative segment durations are treated as zero, so a zero
		///         attack, decay or release produces the corresponding vertical
		///         edge.
		static Envelope adsr(const Number& attackTime, const Number& decayTime,
			const Number& sustainLevel, const Number& holdTime, const Number& releaseTime,
			const Number& peakLevel = static_cast<Number>(1)) {
			const Number attack = std::max(attackTime, Zero<Number>);
			const Number decay = std::max(decayTime, Zero<Number>);
			const Number hold = std::max(holdTime, Zero<Number>);
			const Number release = std::max(releaseTime, Zero<Number>);
			const Number peakTime = attack;
			const Number decayEnd = peakTime + decay;
			const Number holdEnd = decayEnd + hold;
			const Number releaseEnd = holdEnd + release;

			std::vector<ControlPoint> points;
			points.reserve(5);
			points.push_back(ControlPoint{Zero<Number>, Zero<Number>});
			points.push_back(ControlPoint{peakTime, peakLevel});
			points.push_back(ControlPoint{decayEnd, sustainLevel});
			points.push_back(ControlPoint{holdEnd, sustainLevel});
			points.push_back(ControlPoint{releaseEnd, Zero<Number>});
			return Envelope(std::move(points));
		}

		/// @brief  Creates an envelope from a uniformly sampled amplitude sequence.
		/// @param  levels          Amplitude of each sample, in time order.
		/// @param  sampleInterval  Spacing between neighbouring samples; should be
		///                         positive.
		/// @param  startTime       Time of the first sample.
		/// @return A piecewise-linear curve through the samples; an empty
		///         sequence yields an empty envelope.
		static Envelope fromSamples(const std::vector<Number>& levels,
			const Number& sampleInterval, const Number& startTime = Zero<Number>) {
			std::vector<ControlPoint> points;
			points.reserve(levels.size());
			for (std::size_t index = 0; index < levels.size(); ++index) {
				const Number time = startTime + static_cast<Number>(index) * sampleInterval;
				points.push_back(ControlPoint{time, levels[index]});
			}
			return Envelope(std::move(points));
		}

		/// @brief  Reports whether the envelope has no control points.
		/// @return True when the envelope is silent and holds no points.
		bool empty() const {
			return controlPoints.empty();
		}

		/// @brief  Number of control points.
		/// @return The number of points that shape the curve.
		std::size_t size() const {
			return controlPoints.size();
		}

		/// @brief  Reads one control point.
		/// @param  index  Zero-based index, which must be smaller than size().
		/// @return The control point, in increasing time order.
		/// @throws std::out_of_range  When @p index is not smaller than size().
		const ControlPoint& controlPoint(std::size_t index) const {
			return controlPoints.at(index);
		}

		/// @brief  Time of the first control point.
		/// @return The earliest time, or zero when the envelope is empty.
		Number startTime() const {
			return empty() ? Zero<Number> : controlPoints.front().time;
		}

		/// @brief  Time of the last control point.
		/// @return The latest time, or zero when the envelope is empty.
		Number endTime() const {
			return empty() ? Zero<Number> : controlPoints.back().time;
		}

		/// @brief  Length of the interval covered by the control points.
		/// @return endTime() - startTime(), or zero when the envelope is empty.
		Number duration() const {
			return endTime() - startTime();
		}

		/// @brief  Evaluates the envelope at a given time.
		/// @param  time  Position on the time axis.
		/// @return The interpolated level. Before the first and after the last
		///         control point the terminal level is returned; an empty
		///         envelope returns zero.
		Number at(const Number& time) const {
			if (controlPoints.empty()) {
				return Zero<Number>;
			}

			if (time <= controlPoints.front().time) {
				return controlPoints.front().level;
			}

			if (time >= controlPoints.back().time) {
				return controlPoints.back().level;
			}

			const auto upper = std::upper_bound(controlPoints.begin(), controlPoints.end(), time,
				[](const Number& value, const ControlPoint& point) {
					return value < point.time;
				});
			const ControlPoint& left = *(upper - 1);
			const ControlPoint& right = *upper;
			const Number span = right.time - left.time;
			const Number position = (time - left.time) / span;
			return left.level + (right.level - left.level) * position;
		}

		/// @brief  Evaluates the envelope, so that it reads like a function.
		/// @param  time  Position on the time axis.
		/// @return The interpolated level, as returned by at().
		Number operator()(const Number& time) const {
			return at(time);
		}

		/// @brief  Rate of change of the envelope at a time.
		/// @param  time  Position on the time axis.
		/// @return The slope of the segment that covers @p time; zero outside the
		///         control-point interval, where the level is held constant, and
		///         for an envelope with fewer than two points.
		/// @note   At a control point the right-hand segment is used, so the value
		///         is the one-sided derivative from above.
		Number slopeAt(const Number& time) const {
			if (controlPoints.size() < 2) {
				return Zero<Number>;
			}
			if (time < controlPoints.front().time || time >= controlPoints.back().time) {
				return Zero<Number>;
			}
			const auto upper = std::upper_bound(controlPoints.begin(), controlPoints.end(), time,
				[](const Number& value, const ControlPoint& point) {
					return value < point.time;
				});
			const ControlPoint& left = *(upper - 1);
			const ControlPoint& right = *upper;
			return (right.level - left.level) / (right.time - left.time);
		}

		/// @brief  Largest control-point level.
		/// @return The maximum level, which is also the maximum of the whole
		///         curve; zero when the envelope is empty.
		Number peak() const {
			if (controlPoints.empty()) {
				return Zero<Number>;
			}
			Number result = controlPoints.front().level;
			for (const ControlPoint& point : controlPoints) {
				result = std::max(result, point.level);
			}
			return result;
		}

		/// @brief  Smallest control-point level.
		/// @return The minimum level, which is also the minimum of the whole
		///         curve; zero when the envelope is empty.
		Number trough() const {
			if (controlPoints.empty()) {
				return Zero<Number>;
			}
			Number result = controlPoints.front().level;
			for (const ControlPoint& point : controlPoints) {
				result = std::min(result, point.level);
			}
			return result;
		}

		/// @brief  Largest absolute control-point level.
		/// @return The peak magnitude, or zero when the envelope is empty.
		Number peakMagnitude() const {
			Number result = Zero<Number>;
			for (const ControlPoint& point : controlPoints) {
				result = std::max(result, std::abs(point.level));
			}
			return result;
		}

		/// @brief  Area under the curve between the first and last point.
		/// @return The integral of the level over time; zero when there are
		///         fewer than two control points.
		/// @note   The constant tails outside the control-point interval are not
		///         included, so the result is always finite.
		Number integral() const {
			Number result = Zero<Number>;
			for (std::size_t index = 1; index < controlPoints.size(); ++index) {
				const ControlPoint& left = controlPoints[index - 1];
				const ControlPoint& right = controlPoints[index];
				const Number width = right.time - left.time;
				result += width * (left.level + right.level) / static_cast<Number>(2);
			}
			return result;
		}

		/// @brief  Average level over the control-point interval.
		/// @return integral() divided by duration(); the held level of a
		///         single-point envelope; zero when the envelope is empty.
		Number mean() const {
			if (controlPoints.empty()) {
				return Zero<Number>;
			}
			const Number span = duration();
			if (span == Zero<Number>) {
				return controlPoints.front().level;
			}
			return integral() / span;
		}

		/// @brief  Samples the envelope on a uniform time grid.
		/// @param  startTime       Time of the first sample.
		/// @param  sampleInterval  Spacing between neighbouring samples; should be
		///                         positive.
		/// @param  count           Number of samples to take.
		/// @return One level per sample, each obtained through at().
		std::vector<Number> toSamples(const Number& startTime, const Number& sampleInterval,
			std::size_t count) const {
			std::vector<Number> result;
			result.reserve(count);
			for (std::size_t index = 0; index < count; ++index) {
				const Number time = startTime + static_cast<Number>(index) * sampleInterval;
				result.push_back(at(time));
			}
			return result;
		}

		/// @brief  Returns the envelope scaled to unit peak magnitude.
		/// @return A copy whose largest absolute level is one; an envelope that
		///         is zero everywhere is returned unchanged.
		Envelope normalized() const {
			const Number scale = peakMagnitude();
			if (scale == Zero<Number>) {
				return *this;
			}
			return scaled(static_cast<Number>(1) / scale);
		}

		/// @brief  Returns a copy with every level raised by an offset.
		/// @param  offset  Value added to each control-point level.
		/// @return A new, shifted envelope.
		Envelope shifted(const Number& offset) const {
			Envelope result(*this);
			result += offset;
			return result;
		}

		/// @brief  Returns a copy with every level multiplied by a factor.
		/// @param  factor  Multiplier applied to each control-point level.
		/// @return A new, scaled envelope.
		Envelope scaled(const Number& factor) const {
			Envelope result(*this);
			result *= factor;
			return result;
		}

		/// @brief  Returns a copy with negative levels replaced by zero.
		/// @return A new envelope in which every level is non-negative.
		Envelope rectified() const {
			Envelope result(*this);
			for (ControlPoint& point : result.controlPoints) {
				point.level = std::max(point.level, Zero<Number>);
			}
			return result;
		}

		/// @brief  Raises every control-point level by an offset.
		/// @param  offset  Value added to each level.
		/// @return Reference to this envelope.
		/// @note   Shifting a silent envelope by a non-zero offset turns it into
		///         the corresponding constant envelope, held by a control point
		///         at time zero.
		Envelope& operator+=(const Number& offset) {
			if (controlPoints.empty()) {
				if (offset != Zero<Number>) {
					controlPoints.push_back(ControlPoint{Zero<Number>, offset});
				}
				return *this;
			}
			for (ControlPoint& point : controlPoints) {
				point.level += offset;
			}
			return *this;
		}

		/// @brief  Lowers every control-point level by an offset.
		/// @param  offset  Value subtracted from each level.
		/// @return Reference to this envelope.
		/// @note   Shifting a silent envelope by a non-zero offset turns it into
		///         the corresponding constant envelope, held by a control point
		///         at time zero.
		Envelope& operator-=(const Number& offset) {
			if (controlPoints.empty()) {
				if (offset != Zero<Number>) {
					controlPoints.push_back(ControlPoint{Zero<Number>, -offset});
				}
				return *this;
			}
			for (ControlPoint& point : controlPoints) {
				point.level -= offset;
			}
			return *this;
		}

		/// @brief  Scales every control-point level by a factor.
		/// @param  factor  Multiplier.
		/// @return Reference to this envelope.
		Envelope& operator*=(const Number& factor) {
			for (ControlPoint& point : controlPoints) {
				point.level *= factor;
			}
			return *this;
		}

		/// @brief  Divides every control-point level by a divisor.
		/// @param  divisor  Divisor.
		/// @return Reference to this envelope.
		/// @warning Undefined when @p divisor is zero.
		Envelope& operator/=(const Number& divisor) {
			for (ControlPoint& point : controlPoints) {
				point.level /= divisor;
			}
			return *this;
		}

		/// @brief  Returns a copy with every level raised by an offset.
		/// @param  offset  Value added to each level.
		/// @return A new, shifted envelope.
		Envelope operator+(const Number& offset) const {
			return shifted(offset);
		}

		/// @brief  Returns a copy with every level lowered by an offset.
		/// @param  offset  Value subtracted from each level.
		/// @return A new, shifted envelope.
		Envelope operator-(const Number& offset) const {
			return shifted(-offset);
		}

		/// @brief  Returns a copy with every level multiplied by a factor.
		/// @param  factor  Multiplier.
		/// @return A new, scaled envelope.
		Envelope operator*(const Number& factor) const {
			return scaled(factor);
		}

		/// @brief  Returns a copy with every level divided by a divisor.
		/// @param  divisor  Divisor.
		/// @return A new, divided envelope.
		/// @warning Undefined when @p divisor is zero.
		Envelope operator/(const Number& divisor) const {
			Envelope result(*this);
			result /= divisor;
			return result;
		}

		/// @brief  Returns the envelope with every level negated.
		/// @return A new, negated envelope.
		Envelope operator-() const {
			Envelope result(*this);
			for (ControlPoint& point : result.controlPoints) {
				point.level = -point.level;
			}
			return result;
		}

		/// @brief  Returns the envelope unchanged.
		/// @return A copy of this envelope.
		Envelope operator+() const {
			return *this;
		}

		/// @brief  Tests whether two envelopes describe the same curve.
		/// @param  other    Envelope to compare with.
		/// @param  epsilon  Relative tolerance; defaults to eight times the
		///                  machine epsilon of @p Number.
		/// @return True when both curves evaluate to the same level, to within
		///         @p epsilon, at every control point of either envelope.
		/// @note   The comparison is carried out on values, not on the stored
		///         points, so two envelopes with different but collinear control
		///         points still compare equal.
		bool isApproximately(const Envelope& other,
			const Number& epsilon = std::numeric_limits<Number>::epsilon() * static_cast<Number>(8)) const {
			for (const ControlPoint& point : controlPoints) {
				if (!approximatelyEqual(point.level, other.at(point.time), epsilon)) {
					return false;
				}
			}
			for (const ControlPoint& point : other.controlPoints) {
				if (!approximatelyEqual(point.level, at(point.time), epsilon)) {
					return false;
				}
			}
			return true;
		}

		/// @brief  Equality test, using the approximate comparison above.
		/// @param  other  Envelope to compare with.
		/// @return True when the curves agree to within the default tolerance.
		bool operator==(const Envelope& other) const {
			return isApproximately(other);
		}

		/// @brief  Inequality test.
		/// @param  other  Envelope to compare with.
		/// @return True when the curves differ by more than the default tolerance.
		bool operator!=(const Envelope& other) const {
			return !(*this == other);
		}
	};

	/// @brief  Scalar-first multiplication, so that 2 * envelope reads naturally.
	/// @param  factor    Multiplier.
	/// @param  envelope  Envelope to scale.
	/// @return The scaled envelope.
	/// @tparam Number  Arithmetic type used for times and levels.
	/// @tparam Scalar  Arithmetic type of the multiplier.
	template<typename Number, typename Scalar,
		typename = std::enable_if_t<std::is_arithmetic<Scalar>::value>>
	Envelope<Number> operator*(const Scalar& factor, const Envelope<Number>& envelope) {
		return envelope * factor;
	}

	/// @brief  Writes an envelope to a stream as its control-point list.
	/// @param  stream    Destination stream.
	/// @param  envelope  Envelope to write.
	/// @return The stream, so that insertions can be chained.
	/// @note   An envelope through (0, 0), (1, 1) prints as
	///         "envelope[0: 0, 1: 1]"; an empty envelope prints as
	///         "envelope[]".
	/// @tparam Number  Arithmetic type used for times and levels.
	template<typename Number>
	std::ostream& operator<<(std::ostream& stream, const Envelope<Number>& envelope) {
		stream << "envelope[";
		for (std::size_t index = 0; index < envelope.size(); ++index) {
			if (index != 0) {
				stream << ", ";
			}
			const typename Envelope<Number>::ControlPoint& point = envelope.controlPoint(index);
			stream << point.time << ": " << point.level;
		}
		stream << ']';
		return stream;
	}
}
