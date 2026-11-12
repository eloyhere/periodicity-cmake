/// @file   waveform.h
/// @brief  A sum of rotating phasors, evaluated, differentiated and integrated
///         as a whole.

#pragma once

#include <md/mathematic/epicycle.h>

#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

namespace md::mathematic {

	/// @brief  A sum of Epicycle components, S(t) = sum of A_k(t) e^{i(2 pi f_k t + phi_k)}.
	///
	/// The type is a spectral representation, not a ring buffer: it answers "what
	/// is the value at this time" rather than "what were the last N samples".
	/// Real-time display buffers belong to the stream layer.
	///
	/// @tparam Number  Floating-point type used for time and amplitude.
	template<typename Number = double>
	class Waveform {
	private:
		std::vector<Epicycle<Number>> components;	///< The phasors being summed.

	public:
		/// @brief  Constructs a silent waveform.
		Waveform() = default;

		/// @brief  Constructs a waveform from its components.
		/// @param  source  Phasors to sum; the vector is consumed.
		explicit Waveform(std::vector<Epicycle<Number>> source) : components(std::move(source)) {}

		/// @brief  Constructs a waveform from a brace-enclosed component list.
		Waveform(std::initializer_list<Epicycle<Number>> source) : components(source) {}

		/// @brief  The components being summed.
		const std::vector<Epicycle<Number>>& epicycles() const {
			return components;
		}

		/// @brief  One component.
		/// @param  index  Zero-based index, which must be smaller than size().
		const Epicycle<Number>& epicycle(const std::size_t index) const {
			return components.at(index);
		}

		/// @brief  Number of components.
		std::size_t size() const {
			return components.size();
		}

		/// @brief  Reports whether the waveform holds no components.
		bool empty() const {
			return components.empty();
		}

		/// @brief  Appends a component to the sum.
		/// @param  component  Phasor to add.
		void add(const Epicycle<Number>& component) {
			components.push_back(component);
		}

		/// @brief  The value of the sum at a time.
		Complex<Number> valueAt(const Number& time) const {
			Complex<Number> total;
			for (const Epicycle<Number>& component : components) {
				total += component.valueAt(time);
			}
			return total;
		}

		/// @brief  The exact derivative of the sum at a time.
		Complex<Number> slopeAt(const Number& time) const {
			Complex<Number> total;
			for (const Epicycle<Number>& component : components) {
				total += component.slopeAt(time);
			}
			return total;
		}

		/// @brief  The carrier derivative of the sum.
		/// @return A waveform holding the carrier derivative of every component.
		/// @note   Amplitude envelopes are kept unchanged; use slopeAt() for the
		///         exact pointwise derivative of a modulated waveform.
		Waveform derivate() const {
			Waveform result;
			result.components.reserve(components.size());
			for (const Epicycle<Number>& component : components) {
				result.components.push_back(component.derivate());
			}
			return result;
		}

		/// @brief  The exact integral of the sum over an interval.
		Complex<Number> integral(const Number& start, const Number& end) const {
			Complex<Number> total;
			for (const Epicycle<Number>& component : components) {
				total += component.integral(start, end);
			}
			return total;
		}

		/// @brief  The integral of the sum from time zero to a time.
		/// @param  time  Upper bound, in seconds.
		Complex<Number> integralAt(const Number& time) const {
			return integral(Zero<Number>, time);
		}

		/// @brief  Adds another waveform to this one.
		Waveform& operator+=(const Waveform& other) {
			components.insert(components.end(), other.components.begin(), other.components.end());
			return *this;
		}

		/// @brief  Returns the sum of two waveforms.
		Waveform operator+(const Waveform& other) const {
			Waveform result(*this);
			result += other;
			return result;
		}

		/// @brief  Scales every component.
		Waveform& operator*=(const Number& factor) {
			for (Epicycle<Number>& component : components) {
				component = Epicycle<Number>(component.envelope().scaled(factor), component.carrier(),
					component.initialPhase());
			}
			return *this;
		}

		/// @brief  Returns the waveform scaled by a factor.
		Waveform operator*(const Number& factor) const {
			Waveform result(*this);
			result *= factor;
			return result;
		}
	};
}
