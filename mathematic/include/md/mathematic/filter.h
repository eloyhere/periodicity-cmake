/// @file   filter.h
/// @brief  Second-order IIR filters with Butterworth and notch designs, a
///         streaming chain, and zero-phase forward-backward filtering.
///

#pragma once

#include <md/mathematic/frequency.h>
#include <md/mathematic/numerical.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace md::mathematic {

	/// @brief  One second-order section, the building block of every IIR filter here.
	///
	/// The transfer function is (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2),
	/// so the recurrence is
	///   y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2].
	/// A first-order section is represented by setting b2 = a2 = 0.
	///
	/// @tparam Number  Floating-point type used for coefficients and samples.
	template<typename Number = double>
	class Biquad {
	public:
		/// @brief  The five coefficients of the section, with a0 already normalised to one.
		struct Coefficients {
			Number b0 = Number(1);	///< Feed-forward, current sample.
			Number b1 = Number(0);	///< Feed-forward, one sample back.
			Number b2 = Number(0);	///< Feed-forward, two samples back.
			Number a1 = Number(0);	///< Feedback, one sample back.
			Number a2 = Number(0);	///< Feedback, two samples back.
		};

	private:
		Coefficients values;	///< Transfer-function coefficients.
		Number input1 = Number(0);	///< x[n-1].
		Number input2 = Number(0);	///< x[n-2].
		Number output1 = Number(0);	///< y[n-1].
		Number output2 = Number(0);	///< y[n-2].

	public:
		/// @brief  Constructs a pass-through section.
		Biquad() = default;

		/// @brief  Constructs a section from its coefficients.
		explicit Biquad(const Coefficients& coefficients) : values(coefficients) {}

		/// @brief  The coefficients.
		const Coefficients& coefficients() const {
			return values;
		}

		/// @brief  Replaces the coefficients and clears the state.
		void setCoefficients(const Coefficients& coefficients) {
			values = coefficients;
			reset();
		}

		/// @brief  Clears the delay line.
		void reset() {
			input1 = input2 = output1 = output2 = Number(0);
		}

		/// @brief  Filters one sample and advances the state.
		/// @param  sample  Input x[n].
		/// @return Output y[n].
		Number process(const Number& sample) {
			const Number output = values.b0 * sample + values.b1 * input1 + values.b2 * input2
				- values.a1 * output1 - values.a2 * output2;
			input2 = input1;
			input1 = sample;
			output2 = output1;
			output1 = output;
			return output;
		}

		/// @brief  The magnitude response at one frequency.
		/// @param  frequency   Frequency to evaluate.
		/// @param  sampleRate  Sampling rate in hertz.
		/// @return |H(e^{i omega})| at that frequency.
		Number gainAt(const Frequency<Number>& frequency, const Number& sampleRate) const {
			const Number omega = DoublePi<Number> * frequency.hertz() / sampleRate;
			const Complex<Number> unit(static_cast<Number>(std::cos(omega)),
				-static_cast<Number>(std::sin(omega)));
			const Complex<Number> numerator = Complex<Number>(values.b0, Number(0))
				+ Complex<Number>(values.b1, Number(0)) * unit
				+ Complex<Number>(values.b2, Number(0)) * unit * unit;
			const Complex<Number> denominator = Complex<Number>(Number(1), Number(0))
				+ Complex<Number>(values.a1, Number(0)) * unit
				+ Complex<Number>(values.a2, Number(0)) * unit * unit;
			return numerator.magnitude() / denominator.magnitude();
		}
	};

	/// @brief  A cascade of second-order sections.
	///
	/// @tparam Number  Floating-point type used for coefficients and samples.
	template<typename Number = double>
	class FilterChain {
	private:
		std::vector<Biquad<Number>> stages;	///< Sections applied in order.

	public:
		/// @brief  Constructs an empty chain, which passes samples through unchanged.
		FilterChain() = default;

		/// @brief  Constructs a chain from its sections.
		explicit FilterChain(std::vector<Biquad<Number>> sections) : stages(std::move(sections)) {}

		/// @brief  The sections.
		const std::vector<Biquad<Number>>& sections() const {
			return stages;
		}

		/// @brief  The filter order, two per section.
		std::size_t order() const {
			return stages.size() * 2;
		}

		/// @brief  Clears the state of every section.
		void reset() {
			for (Biquad<Number>& stage : stages) {
				stage.reset();
			}
		}

		/// @brief  Filters one sample through every section.
		Number process(const Number& sample) {
			Number value = sample;
			for (Biquad<Number>& stage : stages) {
				value = stage.process(value);
			}
			return value;
		}

		/// @brief  The overall magnitude response.
		Number gainAt(const Frequency<Number>& frequency, const Number& sampleRate) const {
			Number gain = Number(1);
			for (const Biquad<Number>& stage : stages) {
				gain *= stage.gainAt(frequency, sampleRate);
			}
			return gain;
		}
	};

	/// @brief  A second-order notch (band-stop) section.
	/// @param  centre      Frequency to reject.
	/// @param  quality     Q factor; 30 gives a narrow notch, 10 a wider one.
	/// @param  sampleRate  Sampling rate in hertz.
	/// @return The section, normalised so the passband gain is one.
	template<typename Number = double>
	Biquad<Number> notch(const Frequency<Number>& centre, const Number& quality, const Number& sampleRate) {
		if (!(quality > Number(0))) {
			throw std::invalid_argument("notch: the Q factor must be positive");
		}
		const Number omega = DoublePi<Number> * centre.hertz() / sampleRate;
		const Number cosine = static_cast<Number>(std::cos(omega));
		const Number alpha = static_cast<Number>(std::sin(omega)) / (Number(2) * quality);
		const Number a0 = Number(1) + alpha;

		typename Biquad<Number>::Coefficients coefficients;
		coefficients.b0 = Number(1) / a0;
		coefficients.b1 = Number(-2) * cosine / a0;
		coefficients.b2 = Number(1) / a0;
		coefficients.a1 = Number(-2) * cosine / a0;
		coefficients.a2 = (Number(1) - alpha) / a0;
		return Biquad<Number>(coefficients);
	}

	/// @brief  Collects the Butterworth poles of a given order in the z plane.
	/// @param  order       Filter order.
	/// @param  cutoff      Cutoff frequency that the branch is designed for.
	/// @param  sampleRate  Sampling rate in hertz.
	/// @return The digital poles, in no particular order.
	template<typename Number = double>
	std::vector<Complex<Number>> butterworthPoles(const unsigned order, const Frequency<Number>& cutoff,
		const Number& sampleRate) {
		if (order == 0) {
			throw std::invalid_argument("butterworth: the order must be positive");
		}
		if (!(cutoff.hertz() > Number(0)) || !(cutoff.hertz() < sampleRate / Number(2))) {
			throw std::invalid_argument("butterworth: the cutoff must lie between zero and Nyquist");
		}

		// Pre-warped bilinear transform: tan(pi fc / fs) maps the analogue
		// prototype pole s_k to the digital pole (1 + s_k t) / (1 - s_k t).
		const Number tangent = static_cast<Number>(std::tan(Pi<Number> * cutoff.hertz() / sampleRate));
		std::vector<Complex<Number>> poles;
		poles.reserve(order);
		for (unsigned index = 0; index < order; ++index) {
			const Number angle = Pi<Number> * static_cast<Number>(2 * index + order + 1)
				/ static_cast<Number>(2 * order);
			const Complex<Number> prototype(static_cast<Number>(std::cos(angle)),
				static_cast<Number>(std::sin(angle)));
			const Complex<Number> numerator = Complex<Number>(Number(1), Number(0)) + prototype * tangent;
			const Complex<Number> denominator = Complex<Number>(Number(1), Number(0)) - prototype * tangent;
			poles.push_back(numerator / denominator);
		}
		return poles;
	}

	/// @brief  Pairs poles and builds sections, normalising the gain afterwards.
	/// @param  poles        Digital poles, conjugates included.
	/// @param  highPass     True to place zeros at Nyquist, false to place them at DC.
	/// @return The cascade, with passband gain one.
	template<typename Number = double>
	std::vector<Biquad<Number>> buildSections(const std::vector<Complex<Number>>& poles, const bool highPass) {
		std::vector<bool> used(poles.size(), false);
		std::vector<Biquad<Number>> sections;

		for (std::size_t index = 0; index < poles.size(); ++index) {
			if (used[index]) {
				continue;
			}
			const Complex<Number> pole = poles[index];
			used[index] = true;

			typename Biquad<Number>::Coefficients coefficients;
			if (std::abs(pole.imaginary()) > Number(1e-12)) {
				// Find the conjugate partner among the unused poles.
				std::size_t partner = poles.size();
				Number best = std::numeric_limits<Number>::max();
				for (std::size_t other = index + 1; other < poles.size(); ++other) {
					if (used[other]) {
						continue;
					}
					const Number distance = std::abs(poles[other].real() - pole.real())
						+ std::abs(poles[other].imaginary() + pole.imaginary());
					if (distance < best) {
						best = distance;
						partner = other;
					}
				}
				if (partner == poles.size()) {
					throw std::domain_error("buildSections: a complex pole has no conjugate partner");
				}
				used[partner] = true;
				coefficients.a1 = Number(-2) * pole.real();
				coefficients.a2 = pole.magnitudeSquared();
				if (highPass) {
					coefficients.b0 = Number(1);
					coefficients.b1 = Number(-2);
					coefficients.b2 = Number(1);
				}
				else {
					coefficients.b0 = Number(1);
					coefficients.b1 = Number(2);
					coefficients.b2 = Number(1);
				}
			}
			else {
				// A real pole gives a first-order section.
				coefficients.a1 = -pole.real();
				coefficients.a2 = Number(0);
				if (highPass) {
					coefficients.b0 = Number(1);
					coefficients.b1 = Number(-1);
					coefficients.b2 = Number(0);
				}
				else {
					coefficients.b0 = Number(1);
					coefficients.b1 = Number(1);
					coefficients.b2 = Number(0);
				}
			}
			sections.push_back(Biquad<Number>(coefficients));
		}

		// Normalise the cascade: DC for a low-pass, Nyquist for a high-pass.
		const Number probe = highPass ? Number(-1) : Number(1);
		Number total = Number(1);
		for (const Biquad<Number>& section : sections) {
			const auto& c = section.coefficients();
			const Number numerator = c.b0 + c.b1 * probe + c.b2 * probe * probe;
			const Number denominator = Number(1) + c.a1 * probe + c.a2 * probe * probe;
			if (denominator == Number(0)) {
				throw std::domain_error("buildSections: the cascade is singular at the normalisation point");
			}
			total *= numerator / denominator;
		}
		if (total == Number(0)) {
			throw std::domain_error("buildSections: the cascade has zero gain at the normalisation point");
		}
		if (!sections.empty()) {
			typename Biquad<Number>::Coefficients scaled = sections.front().coefficients();
			scaled.b0 /= total;
			scaled.b1 /= total;
			scaled.b2 /= total;
			sections.front().setCoefficients(scaled);
		}
		return sections;
	}

	/// @brief  A Butterworth low-pass filter.
	/// @param  order       Filter order; one section per two orders.
	/// @param  cutoff      -3 dB cutoff frequency.
	/// @param  sampleRate  Sampling rate in hertz.
	/// @return The cascade, DC gain one.
	template<typename Number = double>
	std::vector<Biquad<Number>> butterworthLowPass(const unsigned order, const Frequency<Number>& cutoff,
		const Number& sampleRate) {
		return buildSections(butterworthPoles(order, cutoff, sampleRate), false);
	}

	/// @brief  A Butterworth high-pass filter.
	/// @param  order       Filter order; one section per two orders.
	/// @param  cutoff      -3 dB cutoff frequency.
	/// @param  sampleRate  Sampling rate in hertz.
	/// @return The cascade, Nyquist gain one.
	template<typename Number = double>
	std::vector<Biquad<Number>> butterworthHighPass(const unsigned order, const Frequency<Number>& cutoff,
		const Number& sampleRate) {
		return buildSections(butterworthPoles(order, cutoff, sampleRate), true);
	}

	/// @brief  A band-pass filter made of a high-pass and a low-pass in cascade.
	/// @param  order       Order of each side; the composite slopes are twice as steep.
	/// @param  low         Lower -3 dB edge.
	/// @param  high        Upper -3 dB edge.
	/// @param  sampleRate  Sampling rate in hertz.
	/// @return The cascade, normalised to unit gain at the geometric centre.
	/// @note   This is not a transformed Butterworth prototype: the passband is the
	///         product of two maximally flat responses, so the middle of a narrow
	///         band dips below one before normalisation (about 0.43 for an 8-13 Hz
	///         band of order two per side). The cascade is therefore scaled so the
	///         geometric centre has unit gain; without that, band power computed
	///         downstream would be biased low.
	template<typename Number = double>
	std::vector<Biquad<Number>> butterworthBandPass(const unsigned order, const Frequency<Number>& low,
		const Frequency<Number>& high, const Number& sampleRate) {
		if (!(low.hertz() > Number(0)) || !(high.hertz() > low.hertz())) {
			throw std::invalid_argument("butterworthBandPass: the edges must satisfy 0 < low < high");
		}
		std::vector<Biquad<Number>> sections = butterworthHighPass(order, low, sampleRate);
		for (const Biquad<Number>& section : butterworthLowPass(order, high, sampleRate)) {
			sections.push_back(section);
		}

		const Frequency<Number> centre = Frequency<Number>::fromHertz(
			static_cast<Number>(std::sqrt(low.hertz() * high.hertz())));
		Number total = Number(1);
		for (const Biquad<Number>& section : sections) {
			total *= section.gainAt(centre, sampleRate);
		}
		if (!(total > Number(0))) {
			throw std::domain_error("butterworthBandPass: the cascade has no gain at the band centre");
		}
		typename Biquad<Number>::Coefficients scaled = sections.front().coefficients();
		scaled.b0 /= total;
		scaled.b1 /= total;
		scaled.b2 /= total;
		sections.front().setCoefficients(scaled);
		return sections;
	}

	/// @brief  Filters a whole signal with fresh filter state.
	/// @param  sections  Cascade to apply.
	/// @param  signal    Input samples.
	/// @return The filtered samples.
	template<typename Number = double>
	std::vector<Number> filterSignal(const std::vector<Biquad<Number>>& sections,
		const std::vector<Number>& signal) {
		std::vector<Biquad<Number>> stages(sections);
		std::vector<Number> result;
		result.reserve(signal.size());
		for (const Number& sample : signal) {
			Number value = sample;
			for (Biquad<Number>& stage : stages) {
				value = stage.process(value);
			}
			result.push_back(value);
		}
		return result;
	}

	/// @brief  Zero-phase filtering by running the cascade forwards then backwards.
	/// @param  sections  Cascade to apply.
	/// @param  signal    Input samples.
	/// @return The filtered samples, with no net phase shift.
	/// @note   The state starts at zero in both passes, so the first and last few
	///         samples carry a start-up transient. This is the offline path; the
	///         streaming path is FilterChain::process() and must stay causal.
	template<typename Number = double>
	std::vector<Number> filtfilt(const std::vector<Biquad<Number>>& sections,
		const std::vector<Number>& signal) {
		if (signal.empty()) {
			return {};
		}
		std::vector<Number> forward = filterSignal(sections, signal);
		std::reverse(forward.begin(), forward.end());
		std::vector<Number> backward = filterSignal(sections, forward);
		std::reverse(backward.begin(), backward.end());
		return backward;
	}
}
