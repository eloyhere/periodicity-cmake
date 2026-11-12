/// @file   erp.h
/// @brief  Event-related potential pipeline: segmentation around markers, baseline
///         correction, trial rejection, averaging and peak measurement.
///

#pragma once

#include <md/mathematic/matrix.h>
#include <md/mathematic/sample.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace md::mathematic {

	/// @brief  The averaged response and its uncertainty.
	///
	/// @tparam Number  Floating-point type used for the samples.
	template<typename Number = double>
	struct ErpResult {
		Matrix<Number> average;			///< channels x samples mean across trials.
		Matrix<Number> standardError;	///< channels x samples standard error across trials.
		std::size_t trials = 0;			///< Number of trials that entered the average.
	};

	/// @brief  A peak found inside a measurement window.
	///
	/// @tparam Number  Floating-point type used for the samples.
	template<typename Number = double>
	struct PeakMeasurement {
		std::size_t index = 0;		///< Sample index of the peak inside the epoch.
		Number amplitude = Number(0);	///< Peak value.
		Number latency = Number(0);	///< Peak time relative to the epoch start.
	};

	/// @brief  Cuts one epoch per marker out of a continuous recording.
	/// @param  data         One row of samples per channel.
	/// @param  onsets       Marker positions, in samples.
	/// @param  startOffset  Samples before the marker included in the epoch; may be negative.
	/// @param  length       Epoch length in samples.
	/// @return One channels x length matrix per marker.
	/// @throws std::invalid_argument  When the montage is empty, ragged, a marker is
	///         out of range or an epoch would run past the recording.
	/// @note   Markers whose epoch would leave the recording are rejected rather than
	///         zero-padded, because silent padding biases any baseline correction.
	template<typename Number = double>
	std::vector<Matrix<Number>> extractEpochs(const std::vector<std::vector<Number>>& data,
		const std::vector<std::size_t>& onsets, const long long startOffset, const std::size_t length) {
		if (data.empty()) {
			throw std::invalid_argument("extractEpochs: the montage is empty");
		}
		const std::size_t samples = data.front().size();
		for (const auto& channel : data) {
			if (channel.size() != samples) {
				throw std::invalid_argument("extractEpochs: the channels differ in length");
			}
		}
		if (length == 0) {
			throw std::invalid_argument("extractEpochs: the epoch length must be positive");
		}

		std::vector<Matrix<Number>> epochs;
		epochs.reserve(onsets.size());
		for (const std::size_t onset : onsets) {
			const long long begin = static_cast<long long>(onset) + startOffset;
			const long long end = begin + static_cast<long long>(length);
			if (begin < 0 || end > static_cast<long long>(samples)) {
				throw std::invalid_argument("extractEpochs: a marker is too close to the recording edge");
			}
			Matrix<Number> epoch(data.size(), length, Number(0));
			for (std::size_t channel = 0; channel < data.size(); ++channel) {
				for (std::size_t index = 0; index < length; ++index) {
					epoch(channel, index) = data[channel][static_cast<std::size_t>(begin + static_cast<long long>(index))];
				}
			}
			epochs.push_back(std::move(epoch));
		}
		return epochs;
	}

	/// @brief  Removes the mean of the first samples of every epoch and channel.
	/// @param  epochs          One channels x samples matrix per epoch.
	/// @param  baselineLength  Samples at the start that define the baseline.
	/// @return The corrected epochs.
	/// @throws std::invalid_argument  When an epoch is shorter than its baseline.
	template<typename Number = double>
	std::vector<Matrix<Number>> baselineCorrect(const std::vector<Matrix<Number>>& epochs,
		const std::size_t baselineLength) {
		if (baselineLength == 0) {
			throw std::invalid_argument("baselineCorrect: the baseline length must be positive");
		}
		std::vector<Matrix<Number>> result;
		result.reserve(epochs.size());
		for (const auto& epoch : epochs) {
			if (epoch.columns() < baselineLength) {
				throw std::invalid_argument("baselineCorrect: an epoch is shorter than the baseline");
			}
			Matrix<Number> corrected(epoch);
			for (std::size_t channel = 0; channel < epoch.rows(); ++channel) {
				Number total = Number(0);
				for (std::size_t index = 0; index < baselineLength; ++index) {
					total += epoch(channel, index);
				}
				const Number offset = total / static_cast<Number>(baselineLength);
				for (std::size_t index = 0; index < epoch.columns(); ++index) {
					corrected(channel, index) -= offset;
				}
			}
			result.push_back(std::move(corrected));
		}
		return result;
	}

	/// @brief  Discards epochs whose peak-to-peak amplitude exceeds a limit.
	/// @param  epochs  Candidate epochs.
	/// @param  limit   Largest tolerated peak-to-peak excursion, in sample units.
	/// @return Indices of the epochs that survive, in input order.
	template<typename Number = double>
	std::vector<std::size_t> rejectByAmplitude(const std::vector<Matrix<Number>>& epochs, const Number& limit) {
		std::vector<std::size_t> kept;
		for (std::size_t index = 0; index < epochs.size(); ++index) {
			const Matrix<Number>& epoch = epochs[index];
			Number smallest = epoch.rows() > 0 ? epoch(0, 0) : Number(0);
			Number largest = smallest;
			for (std::size_t row = 0; row < epoch.rows(); ++row) {
				for (std::size_t column = 0; column < epoch.columns(); ++column) {
					smallest = std::min(smallest, epoch(row, column));
					largest = std::max(largest, epoch(row, column));
				}
			}
			if (largest - smallest <= limit) {
				kept.push_back(index);
			}
		}
		return kept;
	}

	/// @brief  Averages epochs and reports the standard error at every sample.
	/// @param  epochs  One channels x samples matrix per epoch.
	/// @return The mean and standard error across trials.
	/// @throws std::invalid_argument  When the set is empty or the shapes differ.
	template<typename Number = double>
	ErpResult<Number> averageEpochs(const std::vector<Matrix<Number>>& epochs) {
		if (epochs.empty()) {
			throw std::invalid_argument("averageEpochs: the set is empty");
		}
		const std::size_t channels = epochs.front().rows();
		const std::size_t samples = epochs.front().columns();
		for (const auto& epoch : epochs) {
			if (epoch.rows() != channels || epoch.columns() != samples) {
				throw std::invalid_argument("averageEpochs: the epochs differ in shape");
			}
		}

		ErpResult<Number> result;
		result.trials = epochs.size();
		result.average = Matrix<Number>(channels, samples, Number(0));
		result.standardError = Matrix<Number>(channels, samples, Number(0));
		const Number count = static_cast<Number>(epochs.size());

		for (const auto& epoch : epochs) {
			for (std::size_t channel = 0; channel < channels; ++channel) {
				for (std::size_t index = 0; index < samples; ++index) {
					result.average(channel, index) += epoch(channel, index);
				}
			}
		}
		for (std::size_t channel = 0; channel < channels; ++channel) {
			for (std::size_t index = 0; index < samples; ++index) {
				result.average(channel, index) /= count;
			}
		}

		if (epochs.size() > 1) {
			for (const auto& epoch : epochs) {
				for (std::size_t channel = 0; channel < channels; ++channel) {
					for (std::size_t index = 0; index < samples; ++index) {
						const Number difference = epoch(channel, index) - result.average(channel, index);
						result.standardError(channel, index) += difference * difference;
					}
				}
			}
			const Number denominator = count * (count - Number(1));
			for (std::size_t channel = 0; channel < channels; ++channel) {
				for (std::size_t index = 0; index < samples; ++index) {
					result.standardError(channel, index) = static_cast<Number>(
						std::sqrt(result.standardError(channel, index) / denominator));
				}
			}
		}
		return result;
	}

	/// @brief  The mean amplitude of one channel inside a window.
	/// @throws std::invalid_argument  When the window is empty or out of range.
	template<typename Number = double>
	Number meanAmplitude(const Matrix<Number>& waveform, const std::size_t channel,
		const std::size_t begin, const std::size_t end) {
		if (channel >= waveform.rows()) {
			throw std::invalid_argument("meanAmplitude: the channel index is out of range");
		}
		if (end <= begin || end > waveform.columns()) {
			throw std::invalid_argument("meanAmplitude: the window is empty or out of range");
		}
		Number total = Number(0);
		for (std::size_t index = begin; index < end; ++index) {
			total += waveform(channel, index);
		}
		return total / static_cast<Number>(end - begin);
	}

	/// @brief  Finds the largest (or smallest) sample inside a window.
	/// @param  waveform    channels x samples waveform, typically an averaged ERP.
	/// @param  channel     Channel to measure.
	/// @param  begin       First sample of the window.
	/// @param  end         One past the last sample of the window.
	/// @param  sampleRate  Sampling rate, used to express latency in seconds.
	/// @param  positive    True for a positive peak, false for a negative one.
	/// @return Peak index, amplitude and latency relative to the epoch start.
	/// @throws std::invalid_argument  When the window or channel is invalid.
	template<typename Number = double>
	PeakMeasurement<Number> peakInWindow(const Matrix<Number>& waveform, const std::size_t channel,
		const std::size_t begin, const std::size_t end, const SampleRate<Number>& sampleRate,
		const bool positive = true) {
		if (channel >= waveform.rows()) {
			throw std::invalid_argument("peakInWindow: the channel index is out of range");
		}
		if (end <= begin || end > waveform.columns()) {
			throw std::invalid_argument("peakInWindow: the window is empty or out of range");
		}
		if (!sampleRate.isValid()) {
			throw std::invalid_argument("peakInWindow: the sample rate must be positive");
		}

		PeakMeasurement<Number> result;
		result.index = begin;
		result.amplitude = waveform(channel, begin);
		for (std::size_t index = begin + 1; index < end; ++index) {
			const Number value = waveform(channel, index);
			if (positive ? (value > result.amplitude) : (value < result.amplitude)) {
				result.amplitude = value;
				result.index = index;
			}
		}
		result.latency = static_cast<Number>(result.index) / sampleRate.hertz();
		return result;
	}
}
