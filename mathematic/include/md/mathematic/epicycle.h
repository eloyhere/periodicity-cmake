/// @file   epicycle.h
/// @brief  A single rotating phasor whose amplitude may vary over time,
///         together with its value, slope, carrier derivative and exact
///         definite integral.

#pragma once

#include <md/mathematic/angle.h>
#include <md/mathematic/envelope.h>
#include <md/mathematic/frequency.h>
#include <md/mathematic/numerical.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace md::mathematic {

	/// @brief  A rotating phasor A(t) e^{i(2 pi f t + phi)}.
	///
	/// The amplitude is an Envelope over time in seconds, so the component may be
	/// tapered, gated or amplitude modulated while keeping a single carrier
	/// frequency. This is the "component" of which a Waveform is the sum.
	///
	/// Two kinds of differentiation are offered and they are not the same:
	///  - slopeAt() is the exact derivative at a point, including dA/dt;
	///  - derivate() is the carrier derivative, which keeps the amplitude envelope
	///    unchanged and is therefore exact only where the amplitude is constant.
	/// The second exists because the derivative of a single phasor is again a
	/// single phasor, which is what makes a Fourier series easy to differentiate.
	///
	/// @tparam Number  Floating-point type used for time and amplitude.
	template<typename Number = double>
	class Epicycle {
	private:
		Envelope<Number> amplitude;	///< Amplitude envelope over time, in seconds.
		Frequency<Number> frequency;	///< Carrier frequency, in hertz.
		Angle<Number> phase;			///< Phase at time zero.

	public:
		/// @brief  Constructs a silent component at zero hertz.
		Epicycle() = default;

		/// @brief  Constructs a component from its three defining quantities.
		/// @param  amplitude  Amplitude envelope over time in seconds.
		/// @param  frequency  Carrier frequency in hertz.
		/// @param  angle      Phase at time zero.
		Epicycle(Envelope<Number> amplitude, const Frequency<Number>& frequency,
			const Angle<Number>& angle)
			: amplitude(std::move(amplitude)), frequency(frequency), phase(angle) {}

		/// @brief  Creates a component with a constant amplitude.
		/// @param  level  Constant amplitude.
		/// @param  frequency  Carrier frequency in hertz.
		/// @param  angle  Phase at time zero.
		/// @return The component.
		static Epicycle fromConstant(const Number& level, const Frequency<Number>& frequency,
			const Angle<Number>& angle) {
			return Epicycle(Envelope<Number>::constant(level), frequency, angle);
		}

		/// @brief  The amplitude envelope.
		const Envelope<Number>& envelope() const {
			return amplitude;
		}

		/// @brief  The carrier frequency.
		const Frequency<Number>& carrier() const {
			return frequency;
		}

		/// @brief  The phase at time zero.
		const Angle<Number>& initialPhase() const {
			return phase;
		}

		/// @brief  The angular frequency of the carrier.
		Number angularFrequency() const {
			return frequency.radian();
		}

		/// @brief  The amplitude at a time.
		Number amplitudeAt(const Number& time) const {
			return amplitude.at(time);
		}

		/// @brief  The phase at a time.
		Angle<Number> phaseAt(const Number& time) const {
			return phase + Angle<Number>::fromRadian(angularFrequency() * time);
		}

		/// @brief  The value of the phasor at a time.
		Complex<Number> valueAt(const Number& time) const {
			return Complex<Number>::fromPolar(amplitude.at(time), phaseAt(time));
		}

		/// @brief  The exact derivative of the phasor at a time.
		/// @param  time  Position on the time axis, in seconds.
		/// @return (A'(t) + i omega A(t)) e^{i(omega t + phi)}.
		Complex<Number> slopeAt(const Number& time) const {
			const Number level = amplitude.at(time);
			const Number slope = amplitude.slopeAt(time);
			const Number omega = angularFrequency();
			const Angle<Number> angle = phaseAt(time);
			const Number cosine = angle.cosine();
			const Number sine = angle.sine();
			return Complex<Number>(slope * cosine - omega * level * sine,
				slope * sine + omega * level * cosine);
		}

		/// @brief  The carrier derivative, an Epicycle of the same shape.
		/// @return omega A(t) e^{i(omega t + phi + pi/2)}.
		/// @note   The amplitude envelope is kept unchanged, so this ignores dA/dt;
		///         use slopeAt() when the exact pointwise derivative is required.
		Epicycle derivate() const {
			Epicycle result(*this);
			result.amplitude = amplitude.scaled(angularFrequency());
			result.phase = phase + Angle<Number>::fromRadian(HalfPi<Number>);
			return result;
		}

		/// @brief  The carrier antiderivative, an Epicycle of the same shape.
		/// @return A(t)/omega e^{i(omega t + phi - pi/2)}.
		/// @throws std::domain_error  When the carrier frequency is zero.
		Epicycle integral() const {
			const Number omega = angularFrequency();
			if (omega == Zero<Number>) {
				throw std::domain_error("Epicycle::integral: a zero-frequency phasor has no phasor antiderivative");
			}
			Epicycle result(*this);
			result.amplitude = amplitude.scaled(One<Number> / omega);
			result.phase = phase - Angle<Number>::fromRadian(HalfPi<Number>);
			return result;
		}

		/// @brief  The exact definite integral of the phasor over an interval.
		/// @param  start  Start of the interval, in seconds.
		/// @param  end    End of the interval, in seconds.
		/// @return The integral of A(t) e^{i(omega t + phi)} dt between the bounds;
		///         negative when @p end lies before @p start.
		/// @note   The amplitude is piecewise linear, so the integral is evaluated
		///         in closed form on every envelope segment and is exact apart from
		///         rounding.
		Complex<Number> integral(const Number& start, const Number& end) const {
			if (end < start) {
				return -integral(end, start);
			}
			if (end == start) {
				return Complex<Number>();
			}

			std::vector<Number> bounds;
			bounds.reserve(amplitude.size() + 2);
			bounds.push_back(start);
			for (std::size_t index = 0; index < amplitude.size(); ++index) {
				const Number time = amplitude.controlPoint(index).time;
				if (time > start && time < end) {
					bounds.push_back(time);
				}
			}
			bounds.push_back(end);
			std::sort(bounds.begin(), bounds.end());

			const Number omega = angularFrequency();
			Complex<Number> total;

			for (std::size_t index = 1; index < bounds.size(); ++index) {
				const Number first = bounds[index - 1];
				const Number second = bounds[index];
				const Number width = second - first;
				if (width == Zero<Number>) {
					continue;
				}
				const Number levelFirst = amplitude.at(first);
				const Number levelSecond = amplitude.at(second);
				const Number slope = (levelSecond - levelFirst) / width;

				if (omega == Zero<Number>) {
					total += Complex<Number>(width * (levelFirst + levelSecond) / static_cast<Number>(2),
						Zero<Number>);
					continue;
				}

				const Angle<Number> angleFirst = phase + Angle<Number>::fromRadian(omega * first);
				const Angle<Number> angleSecond = phase + Angle<Number>::fromRadian(omega * second);
				const Complex<Number> waveFirst(angleFirst.cosine(), angleFirst.sine());
				const Complex<Number> waveSecond(angleSecond.cosine(), angleSecond.sine());
				const Complex<Number> iOmega(Zero<Number>, omega);

				const Complex<Number> firstMoment = (waveSecond - waveFirst) / iOmega;
				const Complex<Number> secondMoment = (waveSecond * second - waveFirst * first) / iOmega
					- (waveSecond - waveFirst) / (iOmega * iOmega);

				total += waveIndependentTerm(firstMoment, secondMoment, levelFirst, slope, first);
			}
			return total;
		}

	private:
		/// @brief  Combines the two moments into the integral of a linear segment.
		static Complex<Number> waveIndependentTerm(const Complex<Number>& firstMoment,
			const Complex<Number>& secondMoment, const Number& level, const Number& slope,
			const Number& start) {
			return firstMoment * level + (secondMoment - firstMoment * start) * slope;
		}
	};
}
