/// @file   channel_quality.h
/// @brief  Per-channel contact metrics and the rule that turns them into a bad
///         channel mask.
///

#pragma once

#include <md/mathematic/frequency.h>
#include <md/mathematic/tone.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace md::mathematic {

	/// @brief  The reasons a channel can be rejected, combined as bit flags.
	enum class ChannelFlag : unsigned {
		Good = 0u,					///< No problem found.
		Flatline = 1u << 0,			///< The channel hardly changes.
		Clipping = 1u << 1,			///< A large share of samples sits at the rail.
		LineNoise = 1u << 2,		///< Mains interference dominates the channel.
		LowCorrelation = 1u << 3,	///< The channel does not move with the others.
		LowAmplitude = 1u << 4		///< Far quieter than the rest of the montage.
	};

	/// @brief  Combines two flag sets.
	inline ChannelFlag operator|(const ChannelFlag left, const ChannelFlag right) {
		return static_cast<ChannelFlag>(static_cast<unsigned>(left) | static_cast<unsigned>(right));
	}

	/// @brief  Tests whether a flag is present.
	inline bool hasFlag(const ChannelFlag value, const ChannelFlag query) {
		return (static_cast<unsigned>(value) & static_cast<unsigned>(query)) != 0u;
	}

	/// @brief  The measured state of one channel.
	///
	/// @tparam Number  Floating-point type used for the samples.
	template<typename Number = double>
	struct ChannelMetrics {
		Number rms = Number(0);					///< Root mean square amplitude.
		Number peakToPeak = Number(0);			///< Largest minus smallest sample.
		Number lineNoiseRatio = Number(0);		///< Mains power over total power, in [0, 1].
		Number flatlineFraction = Number(0);	///< Share of adjacent equal samples, in [0, 1].
		Number clippingFraction = Number(0);	///< Share of samples at the clip level, in [0, 1].
		Number meanCorrelation = Number(1);		///< Mean absolute correlation with the other channels.
	};

	/// @brief  The thresholds used to classify channels.
	///
	/// Amplitude is judged relative to the median of the montage, so the rule does
	/// not depend on the microvolt scale of a particular recording.
	///
	/// @tparam Number  Floating-point type used for the thresholds.
	template<typename Number = double>
	struct ChannelLimits {
		Number minimumAmplitudeRatio = static_cast<Number>(0.01);	///< RMS over median RMS.
		Number maximumLineNoiseRatio = static_cast<Number>(0.5);	///< Mains share.
		Number minimumCorrelation = static_cast<Number>(0.2);		///< Mean absolute correlation.
		Number maximumFlatlineFraction = static_cast<Number>(0.5);	///< Equal-neighbour share.
		Number maximumClippingFraction = static_cast<Number>(0.001);	///< Rail share.
	};

	/// @brief  Measures every channel of a montage.
	/// @param  channels      One row of samples per channel; rows must be equally long.
	/// @param  sampleRate    Sampling rate in hertz.
	/// @param  lineFrequency Mains frequency to quantify, 50 Hz by default.
	/// @param  clipLevel     Absolute level that counts as the rail; a negative
	///                       value disables clipping detection.
	/// @return One metrics record per channel.
	/// @throws std::invalid_argument  When rows differ in length or the rate is not positive.
	template<typename Number = double>
	std::vector<ChannelMetrics<Number>> measureChannels(const std::vector<std::vector<Number>>& channels,
		const Number& sampleRate, const Frequency<Number>& lineFrequency = Frequency<Number>::fromHertz(50.0),
		const Number& clipLevel = Number(-1)) {
		if (!(sampleRate > Number(0))) {
			throw std::invalid_argument("measureChannels: the sample rate must be positive");
		}
		std::vector<ChannelMetrics<Number>> result;
		if (channels.empty()) {
			return result;
		}

		const std::size_t samples = channels.front().size();
		for (const auto& channel : channels) {
			if (channel.size() != samples) {
				throw std::invalid_argument("measureChannels: the channels differ in length");
			}
		}
		if (samples < 2) {
			throw std::invalid_argument("measureChannels: at least two samples are required");
		}

		const std::size_t count = channels.size();
		std::vector<Number> means(count, Number(0));
		std::vector<Number> deviations(count, Number(0));
		std::vector<Number> powers(count, Number(0));

		for (std::size_t index = 0; index < count; ++index) {
			Number total = Number(0);
			for (const Number& value : channels[index]) {
				total += value;
			}
			means[index] = total / static_cast<Number>(samples);
			Number square = Number(0);
			for (const Number& value : channels[index]) {
				square += value * value;
			}
			powers[index] = square / static_cast<Number>(samples);
			deviations[index] = static_cast<Number>(std::sqrt(std::max(powers[index] - means[index] * means[index],
				Number(0))));
		}

		result.resize(count);
		const Tone<Number> probe(lineFrequency);
		for (std::size_t index = 0; index < count; ++index) {
			ChannelMetrics<Number>& metrics = result[index];
			const std::vector<Number>& channel = channels[index];

			metrics.rms = static_cast<Number>(std::sqrt(powers[index]));
			const auto bounds = std::minmax_element(channel.begin(), channel.end());
			metrics.peakToPeak = *bounds.second - *bounds.first;

			// Mains power from the single-frequency correlation already implemented
			// by Tone; a cosine of amplitude A carries A^2/2 of mean square power.
			const Number lineAmplitude = probe.amplitudeIn(channel, sampleRate);
			const Number linePower = lineAmplitude * lineAmplitude / static_cast<Number>(2);
			metrics.lineNoiseRatio = powers[index] > Number(0)
				? std::min(linePower / powers[index], Number(1))
				: Number(0);

			std::size_t equalNeighbours = 0;
			for (std::size_t sample = 1; sample < samples; ++sample) {
				if (channel[sample] == channel[sample - 1]) {
					++equalNeighbours;
				}
			}
			metrics.flatlineFraction = static_cast<Number>(equalNeighbours) / static_cast<Number>(samples - 1);

			if (clipLevel > Number(0)) {
				std::size_t clipped = 0;
				for (const Number& value : channel) {
					if (std::abs(value) >= clipLevel) {
						++clipped;
					}
				}
				metrics.clippingFraction = static_cast<Number>(clipped) / static_cast<Number>(samples);
			}

			Number correlationTotal = Number(0);
			std::size_t correlationCount = 0;
			for (std::size_t other = 0; other < count; ++other) {
				if (other == index) {
					continue;
				}
				Number covariance = Number(0);
				for (std::size_t sample = 0; sample < samples; ++sample) {
					covariance += (channel[sample] - means[index]) * (channels[other][sample] - means[other]);
				}
				covariance /= static_cast<Number>(samples);
				const Number denominator = deviations[index] * deviations[other];
				if (denominator > Number(0)) {
					correlationTotal += std::abs(covariance / denominator);
					++correlationCount;
				}
			}
			metrics.meanCorrelation = correlationCount > 0
				? correlationTotal / static_cast<Number>(correlationCount)
				: Number(1);
		}
		return result;
	}

	/// @brief  Classifies channels against a set of limits.
	/// @param  metrics  Measurements produced by measureChannels().
	/// @param  limits   Thresholds.
	/// @return One flag set per channel.
	template<typename Number = double>
	std::vector<ChannelFlag> classifyChannels(const std::vector<ChannelMetrics<Number>>& metrics,
		const ChannelLimits<Number>& limits = ChannelLimits<Number>{}) {
		std::vector<ChannelFlag> flags(metrics.size(), ChannelFlag::Good);
		if (metrics.empty()) {
			return flags;
		}

		std::vector<Number> amplitudes;
		amplitudes.reserve(metrics.size());
		for (const ChannelMetrics<Number>& record : metrics) {
			amplitudes.push_back(record.rms);
		}
		std::sort(amplitudes.begin(), amplitudes.end());
		const Number median = amplitudes[amplitudes.size() / 2];

		for (std::size_t index = 0; index < metrics.size(); ++index) {
			const ChannelMetrics<Number>& record = metrics[index];
			ChannelFlag& flag = flags[index];

			if (record.flatlineFraction >= limits.maximumFlatlineFraction) {
				flag = flag | ChannelFlag::Flatline;
			}
			if (record.clippingFraction > limits.maximumClippingFraction) {
				flag = flag | ChannelFlag::Clipping;
			}
			if (record.lineNoiseRatio >= limits.maximumLineNoiseRatio) {
				flag = flag | ChannelFlag::LineNoise;
			}
			if (record.meanCorrelation < limits.minimumCorrelation) {
				flag = flag | ChannelFlag::LowCorrelation;
			}
			if (median > Number(0) && record.rms < median * limits.minimumAmplitudeRatio) {
				flag = flag | ChannelFlag::LowAmplitude;
			}
		}
		return flags;
	}

	/// @brief  The indices of the channels that carry any rejection flag.
	template<typename Number = double>
	std::vector<std::size_t> badChannels(const std::vector<ChannelMetrics<Number>>& metrics,
		const ChannelLimits<Number>& limits = ChannelLimits<Number>{}) {
		const std::vector<ChannelFlag> flags = classifyChannels(metrics, limits);
		std::vector<std::size_t> result;
		for (std::size_t index = 0; index < flags.size(); ++index) {
			if (flags[index] != ChannelFlag::Good) {
				result.push_back(index);
			}
		}
		return result;
	}

	/// @brief  A boolean mask with true for every good channel.
	template<typename Number = double>
	std::vector<bool> goodChannelMask(const std::vector<ChannelMetrics<Number>>& metrics,
		const ChannelLimits<Number>& limits = ChannelLimits<Number>{}) {
		const std::vector<ChannelFlag> flags = classifyChannels(metrics, limits);
		std::vector<bool> mask(flags.size(), true);
		for (std::size_t index = 0; index < flags.size(); ++index) {
			mask[index] = flags[index] == ChannelFlag::Good;
		}
		return mask;
	}
}
