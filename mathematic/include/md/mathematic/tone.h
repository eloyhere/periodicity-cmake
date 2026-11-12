/// @file   tone.h
/// @brief  A pure tone: a constant-amplitude rotating phasor that can be
///         generated as samples and measured inside a real signal.

#pragma once

#include <md/mathematic/angle.h>
#include <md/mathematic/envelope.h>
#include <md/mathematic/epicycle.h>
#include <md/mathematic/frequency.h>
#include <md/mathematic/numerical.h>
#include <md/mathematic/sample.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace md::mathematic {

	/// @brief  A single frequency with a constant amplitude and a phase at time zero.
	///
	/// A Tone is the semantic shell around a constant-amplitude Epicycle: it can
	/// be turned into samples for a stimulus or a reference signal, and it can be
	/// measured inside a recorded signal with the Goertzel algorithm.
	///
	/// Sign convention: the analytic value is A e^{i(omega t + phi)}, and the real
	/// samples are its real part, A cos(omega t + phi). A real cosine of amplitude
	/// A has |X(f)| = A N / 2 at its own frequency, which is the factor of two
	/// that amplitudeIn() removes.
	///
	/// @tparam Number  Floating-point type used for level and time.
	template<typename Number = double>
	class Tone {
	private:
		Frequency<Number> pitchValue;	///< Frequency in hertz.
		Number levelValue;				///< Amplitude, linear, typically one or less.
		Angle<Number> phaseValue;		///< Phase at time zero.

	public:
		/// @brief  Constructs a silent tone at zero hertz.
		Tone() : levelValue(Zero<Number>) {}

		/// @brief  Constructs a tone from its three defining quantities.
		/// @param  pitch         Frequency in hertz.
		/// @param  amplitude     Linear amplitude; may be negative to invert the phase.
		/// @param  initialPhase  Phase at time zero.
		Tone(const Frequency<Number>& pitch, const Number& amplitude = One<Number>,
			const Angle<Number>& initialPhase = Angle<Number>())
			: pitchValue(pitch), levelValue(amplitude), phaseValue(initialPhase) {}

		/// @brief  Creates a tone at an equal-tempered note.
		/// @param  note       MIDI note number, 69 for A4.
		/// @param  amplitude  Linear amplitude.
		/// @return The tone at the note frequency.
		static Tone fromNote(const Number& note, const Number& amplitude = One<Number>) {
			return Tone(Frequency<Number>::fromNote(note), amplitude);
		}

		/// @brief  Creates a tone whose level is given in decibels.
		/// @param  pitch      Frequency in hertz.
		/// @param  decibels   Level in dB relative to @p reference.
		/// @param  reference  Amplitude that corresponds to zero dB.
		/// @return The tone.
		static Tone fromDecibels(const Frequency<Number>& pitch, const Number& decibels,
			const Number& reference = One<Number>) {
			const Number amplitude = reference * static_cast<Number>(std::pow(static_cast<Number>(10),
				decibels / static_cast<Number>(20)));
			return Tone(pitch, amplitude);
		}

		/// @brief  The frequency of the tone.
		const Frequency<Number>& pitch() const {
			return pitchValue;
		}

		/// @brief  The linear amplitude.
		Number amplitude() const {
			return levelValue;
		}

		/// @brief  The phase at time zero.
		const Angle<Number>& initialPhase() const {
			return phaseValue;
		}

		/// @brief  The angular frequency.
		Number angularFrequency() const {
			return pitchValue.radian();
		}

		/// @brief  The level in decibels relative to a reference amplitude.
		/// @param  reference  Amplitude that corresponds to zero dB.
		/// @return Twenty times the base-ten logarithm of amplitude() / reference;
		///         negative infinity for a silent tone.
		Number decibels(const Number& reference = One<Number>) const {
			const Number ratio = std::abs(levelValue) / reference;
			if (!(ratio > Zero<Number>)) {
				return -std::numeric_limits<Number>::infinity();
			}
			return static_cast<Number>(20) * static_cast<Number>(std::log10(ratio));
		}

		/// @brief  The phase of the tone at a time.
		Angle<Number> phaseAt(const Number& time) const {
			return phaseValue + Angle<Number>::fromRadian(angularFrequency() * time);
		}

		/// @brief  The analytic value of the tone at a time.
		Complex<Number> valueAt(const Number& time) const {
			const Angle<Number> angle = phaseAt(time);
			return Complex<Number>::fromPolar(levelValue, angle);
		}

		/// @brief  The real sample of the tone at a time.
		Number sampleAt(const Number& time) const {
			const Angle<Number> angle = phaseAt(time);
			return levelValue * angle.cosine();
		}

		/// @brief  Generates uniformly spaced real samples of the tone.
		/// @param  rate       Sampling rate of the output.
		/// @param  count      Number of samples.
		/// @param  startTime  Time of the first sample, in seconds.
		/// @return count samples of A cos(omega t + phi).
		std::vector<Number> samples(const SampleRate<Number>& rate, const std::size_t count,
			const Number& startTime = Zero<Number>) const {
			std::vector<Number> result(count);
			const Number interval = rate.interval();
			for (std::size_t index = 0; index < count; ++index) {
				result[index] = sampleAt(startTime + static_cast<Number>(index) * interval);
			}
			return result;
		}

		/// @brief  Generates uniformly spaced samples gated by an envelope.
		/// @param  rate       Sampling rate of the output.
		/// @param  count      Number of samples.
		/// @param  startTime  Time of the first sample, in seconds.
		/// @param  gate       Envelope multiplied into every sample, for fades and trains.
		/// @return count samples of gate(t) A cos(omega t + phi).
		std::vector<Number> samples(const SampleRate<Number>& rate, const std::size_t count,
			const Number& startTime, const Envelope<Number>& gate) const {
			std::vector<Number> result(count);
			const Number interval = rate.interval();
			for (std::size_t index = 0; index < count; ++index) {
				const Number time = startTime + static_cast<Number>(index) * interval;
				result[index] = gate.at(time) * sampleAt(time);
			}
			return result;
		}

		/// @brief  Generates uniformly spaced analytic samples of the tone.
		/// @param  rate       Sampling rate of the output.
		/// @param  count      Number of samples.
		/// @param  startTime  Time of the first sample, in seconds.
		std::vector<Complex<Number>> analyticSamples(const SampleRate<Number>& rate,
			const std::size_t count, const Number& startTime = Zero<Number>) const {
			std::vector<Complex<Number>> result(count);
			const Number interval = rate.interval();
			for (std::size_t index = 0; index < count; ++index) {
				result[index] = valueAt(startTime + static_cast<Number>(index) * interval);
			}
			return result;
		}

		/// @brief  The constant-amplitude Epicycle that represents the tone.
		Epicycle<Number> toEpicycle() const {
			return Epicycle<Number>::fromConstant(levelValue, pitchValue, phaseValue);
		}

		/// @brief  Extracts a tone from an Epicycle whose amplitude is constant.
		/// @param  component  Component to read.
		/// @return The equivalent tone.
		/// @throws std::invalid_argument  When the envelope is not constant; use the
		///         Epicycle directly for a modulated phasor.
		/// @throws std::domain_error  When the envelope is empty.
		static Tone fromEpicycle(const Epicycle<Number>& component) {
			if (component.envelope().empty()) {
				throw std::domain_error("Tone::fromEpicycle: the component has no amplitude envelope");
			}
			const Number high = component.envelope().peak();
			const Number low = component.envelope().trough();
			const Number scale = std::max(std::max(std::abs(high), std::abs(low)), One<Number>);
			const Number tolerance = std::numeric_limits<Number>::epsilon() * static_cast<Number>(16) * scale;
			if (std::abs(high - low) > tolerance) {
				throw std::invalid_argument("Tone::fromEpicycle: the component amplitude is not constant");
			}
			return Tone(component.carrier(), high, component.initialPhase());
		}

		/// @brief  The magnitude of the single-frequency correlation.
		/// @param  signal      Real samples at a uniform rate.
		/// @param  sampleRate  Sampling rate of @p signal, in hertz.
		/// @return |X(f)| / N, where X(f) = sum of x[n] e^{-i 2 pi f n / fs}.
		/// @note   Evaluated with the Goertzel recurrence, which costs one multiply
		///         and one add per sample instead of a trigonometric call.
		Number correlationMagnitude(const std::vector<Number>& signal, const Number& sampleRate) const {
			return correlationMagnitudeAt(signal, sampleRate, pitchValue);
		}

		/// @brief  The amplitude of the tone present in a real signal.
		/// @param  signal      Real samples at a uniform rate.
		/// @param  sampleRate  Sampling rate of @p signal, in hertz.
		/// @return Twice correlationMagnitude(), which is the amplitude of a cosine
		///         at the tone frequency.
		Number amplitudeIn(const std::vector<Number>& signal, const Number& sampleRate) const {
			return static_cast<Number>(2) * correlationMagnitude(signal, sampleRate);
		}

		/// @brief  The mean square power carried by the tone inside a signal.
		/// @param  signal      Real samples at a uniform rate.
		/// @param  sampleRate  Sampling rate of @p signal, in hertz.
		/// @return amplitudeIn()^2 / 2.
		Number powerIn(const std::vector<Number>& signal, const Number& sampleRate) const {
			const Number amplitude = amplitudeIn(signal, sampleRate);
			return amplitude * amplitude / static_cast<Number>(2);
		}

		/// @brief  The mean square of a signal.
		/// @param  signal  Real samples.
		/// @return The average of the squared samples; zero for an empty signal.
		static Number meanSquare(const std::vector<Number>& signal) {
			if (signal.empty()) {
				return Zero<Number>;
			}
			Number total = Zero<Number>;
			for (const Number& value : signal) {
				total += value * value;
			}
			return total / static_cast<Number>(signal.size());
		}

		/// @brief  The share of the signal power that sits at the tone frequency.
		/// @param  signal      Real samples at a uniform rate.
		/// @param  sampleRate  Sampling rate of @p signal, in hertz.
		/// @return powerIn() divided by the mean square of @p signal; zero for silence.
		Number relativePowerIn(const std::vector<Number>& signal, const Number& sampleRate) const {
			const Number total = meanSquare(signal);
			if (total <= Zero<Number>) {
				return Zero<Number>;
			}
			return powerIn(signal, sampleRate) / total;
		}

		/// @brief  The mean square power of everything that is not the tone.
		/// @param  signal      Real samples at a uniform rate.
		/// @param  sampleRate  Sampling rate of @p signal, in hertz.
		/// @return The mean square minus the tone power, clamped at zero.
		/// @note   This is a residual, not a spectral estimate: it lumps every
		///         other component, harmonics included, into one number. The
		///         Goertzel recurrence accumulates O(n) rounding, so a residual
		///         below that floor is reported as zero and a numerically pure tone
		///         has no residual at all (roughly, SNR above 130 dB reads as
		///         infinite).
		Number noisePowerIn(const std::vector<Number>& signal, const Number& sampleRate) const {
			const Number total = meanSquare(signal);
			const Number residual = total - powerIn(signal, sampleRate);
			const Number tolerance = total * std::numeric_limits<Number>::epsilon()
				* static_cast<Number>(signal.size()) * static_cast<Number>(16);
			if (!(residual > tolerance)) {
				return Zero<Number>;
			}
			return residual;
		}

		/// @brief  The signal to noise ratio of the tone inside a signal.
		/// @param  signal      Real samples at a uniform rate.
		/// @param  sampleRate  Sampling rate of @p signal, in hertz.
		/// @return Ten times the base-ten logarithm of tone power over residual
		///         power; positive infinity when the residual vanishes, zero when
		///         neither the tone nor the residual carries power.
		Number signalToNoiseRatio(const std::vector<Number>& signal, const Number& sampleRate) const {
			const Number tone = powerIn(signal, sampleRate);
			const Number noise = noisePowerIn(signal, sampleRate);
			if (noise <= Zero<Number>) {
				return tone > Zero<Number> ? std::numeric_limits<Number>::infinity() : Zero<Number>;
			}
			return static_cast<Number>(10) * static_cast<Number>(std::log10(tone / noise));
		}

		/// @brief  The total harmonic distortion of the tone inside a signal.
		/// @param  signal       Real samples at a uniform rate.
		/// @param  sampleRate   Sampling rate of @p signal, in hertz.
		/// @param  maxHarmonic  Highest harmonic considered, at least two.
		/// @return The root sum square of the harmonic amplitudes divided by the
		///         fundamental amplitude; zero when the fundamental is absent.
		/// @note   Harmonics at or above the Nyquist frequency are skipped rather
		///         than aliased, so the result stays meaningful near the band edge.
		Number totalHarmonicDistortion(const std::vector<Number>& signal, const Number& sampleRate,
			const std::size_t maxHarmonic = 5) const {
			const Number fundamental = amplitudeIn(signal, sampleRate);
			if (!(fundamental > Zero<Number>) || maxHarmonic < 2) {
				return Zero<Number>;
			}

			const Number nyquist = sampleRate / static_cast<Number>(2);
			Number harmonicPower = Zero<Number>;
			for (std::size_t harmonic = 2; harmonic <= maxHarmonic; ++harmonic) {
				const Number frequency = pitchValue.hertz() * static_cast<Number>(harmonic);
				if (frequency >= nyquist) {
					break;
				}
				const Number amplitude = static_cast<Number>(2) * correlationMagnitudeAt(signal, sampleRate,
					Frequency<Number>::fromHertz(frequency));
				harmonicPower += amplitude * amplitude;
			}
			return static_cast<Number>(std::sqrt(harmonicPower)) / fundamental;
		}

		/// @brief  Estimates the frequency of a pure tone inside a signal.
		/// @param  signal      Real samples at a uniform rate.
		/// @param  sampleRate  Sampling rate of @p signal, in hertz.
		/// @param  hint        Approximate frequency to search around.
		/// @param  halfWidth   Half the number of one-bin steps searched either side.
		/// @return The refined frequency, interpolated between bins by fitting a
		///         parabola through the three largest correlation magnitudes.
		/// @throws std::invalid_argument  When the signal is empty, the rate is not
		///         positive or @p halfWidth is zero.
		static Frequency<Number> estimateFrequency(const std::vector<Number>& signal,
			const SampleRate<Number>& rate, const Frequency<Number>& hint,
			const std::size_t halfWidth = 2) {
			if (signal.empty()) {
				throw std::invalid_argument("Tone::estimateFrequency: the signal is empty");
			}
			if (!rate.isValid()) {
				throw std::invalid_argument("Tone::estimateFrequency: the sample rate must be positive");
			}
			if (halfWidth == 0) {
				throw std::invalid_argument("Tone::estimateFrequency: the search half-width must be positive");
			}

			const Number resolution = rate.hertz() / static_cast<Number>(signal.size());
			const std::size_t count = 2 * halfWidth + 1;
			std::vector<Number> magnitudes(count);
			std::size_t best = 0;
			for (std::size_t index = 0; index < count; ++index) {
				const Number offset = static_cast<Number>(index) - static_cast<Number>(halfWidth);
				const Frequency<Number> probe = Frequency<Number>::fromHertz(
					hint.hertz() + offset * resolution);
				magnitudes[index] = correlationMagnitudeAt(signal, rate.hertz(), probe);
				if (magnitudes[index] > magnitudes[best]) {
					best = index;
				}
			}

			Number delta = Zero<Number>;
			if (best > 0 && best + 1 < count) {
				const Number left = magnitudes[best - 1];
				const Number centre = magnitudes[best];
				const Number right = magnitudes[best + 1];
				const Number denominator = left - static_cast<Number>(2) * centre + right;
				if (denominator != Zero<Number>) {
					delta = static_cast<Number>(0.5) * (left - right) / denominator;
					delta = std::max(static_cast<Number>(-0.5), std::min(static_cast<Number>(0.5), delta));
				}
			}

			const Number offset = static_cast<Number>(best) - static_cast<Number>(halfWidth) + delta;
			return Frequency<Number>::fromHertz(hint.hertz() + offset * resolution);
		}

		/// @brief  Equality test on pitch, level and phase.
		bool operator==(const Tone& other) const {
			return pitchValue == other.pitchValue && levelValue == other.levelValue
				&& phaseValue == other.phaseValue;
		}

		/// @brief  Inequality test.
		bool operator!=(const Tone& other) const {
			return !(*this == other);
		}

	private:
		/// @brief  The Goertzel magnitude at an arbitrary frequency.
		/// @param  signal      Real samples.
		/// @param  sampleRate  Sampling rate in hertz.
		/// @param  probe       Frequency to evaluate.
		/// @return |sum of x[n] e^{-i 2 pi f n / fs}| / N.
		static Number correlationMagnitudeAt(const std::vector<Number>& signal, const Number& sampleRate,
			const Frequency<Number>& probe) {
			const std::size_t count = signal.size();
			if (count == 0 || !(sampleRate > Zero<Number>)) {
				return Zero<Number>;
			}

			const Number omega = DoublePi<Number> * probe.hertz() / sampleRate;
			const Number coefficient = static_cast<Number>(2) * static_cast<Number>(std::cos(omega));
			Number previous = Zero<Number>;
			Number beforePrevious = Zero<Number>;
			for (const Number& value : signal) {
				const Number current = value + coefficient * previous - beforePrevious;
				beforePrevious = previous;
				previous = current;
			}

			const Number power = previous * previous + beforePrevious * beforePrevious
				- coefficient * previous * beforePrevious;
			return static_cast<Number>(std::sqrt(std::max(power, Zero<Number>)))
				/ static_cast<Number>(count);
		}
	};

	/// @brief  The amplitude of every candidate tone inside one signal.
	/// @param  candidates  Tones to look for, typically the SSVEP stimulus set.
	/// @param  signal      Real samples at a uniform rate.
	/// @param  sampleRate  Sampling rate of @p signal, in hertz.
	/// @return One amplitude per candidate, in the same order.
	/// @note   One pass per candidate, O(candidates * samples); the candidate set
	///         is small and fixed in practice, so no shared transform is built.
	template<typename Number = double>
	std::vector<Number> measureTones(const std::vector<Tone<Number>>& candidates,
		const std::vector<Number>& signal, const Number& sampleRate) {
		std::vector<Number> result(candidates.size());
		for (std::size_t index = 0; index < candidates.size(); ++index) {
			result[index] = candidates[index].amplitudeIn(signal, sampleRate);
		}
		return result;
	}
}
