/// @file   reference.h
/// @brief  Re-referencing schemes: average, Laplacian and bipolar, with bad
///         channels excluded from the reference estimate.
///

#pragma once

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace md::mathematic {

	/// @brief  Re-references a montage against the mean of its good channels.
	///
	/// The mean is computed per sample over the channels the mask marks as good.
	/// Good channels lose that mean; channels marked bad are returned unchanged so
	/// the UI can still display what the electrode actually sees.
	///
	/// @param  channels  One row of samples per channel; rows must be equally long.
	/// @param  goodMask  One flag per channel; empty means every channel is good.
	/// @return The re-referenced montage, with the same shape.
	/// @throws std::invalid_argument  When row lengths differ or the mask size differs.
	template<typename Number = double>
	std::vector<std::vector<Number>> averageReference(const std::vector<std::vector<Number>>& channels,
		const std::vector<bool>& goodMask = {}) {
		if (channels.empty()) {
			return {};
		}
		const std::size_t samples = channels.front().size();
		for (const auto& channel : channels) {
			if (channel.size() != samples) {
				throw std::invalid_argument("averageReference: the channels differ in length");
			}
		}
		if (!goodMask.empty() && goodMask.size() != channels.size()) {
			throw std::invalid_argument("averageReference: the mask size does not match the channels");
		}

		const std::size_t count = channels.size();
		std::vector<bool> good(count, true);
		if (!goodMask.empty()) {
			good = goodMask;
		}
		std::size_t goodCount = 0;
		for (std::size_t index = 0; index < count; ++index) {
			if (good[index]) {
				++goodCount;
			}
		}

		std::vector<std::vector<Number>> result(channels);
		if (goodCount == 0) {
			return result;
		}

		const Number divisor = static_cast<Number>(goodCount);
		for (std::size_t sample = 0; sample < samples; ++sample) {
			Number mean = Number(0);
			for (std::size_t index = 0; index < count; ++index) {
				if (good[index]) {
					mean += channels[index][sample];
				}
			}
			mean /= divisor;
			for (std::size_t index = 0; index < count; ++index) {
				if (good[index]) {
					result[index][sample] -= mean;
				}
			}
		}
		return result;
	}

	/// @brief  Applies a small-surface Laplacian to every good channel.
	///
	/// Each good channel has the mean of its good neighbours subtracted; a channel
	/// without good neighbours is left unchanged. The adjacency is supplied by the
	/// caller because the geometry belongs to the montage, not to this header.
	///
	/// @param  channels    One row of samples per channel.
	/// @param  neighbours  Per-channel neighbour indices.
	/// @param  goodMask    One flag per channel; empty means every channel is good.
	/// @return The re-referenced montage.
	/// @throws std::invalid_argument  When the shapes or indices are inconsistent.
	template<typename Number = double>
	std::vector<std::vector<Number>> laplacianReference(const std::vector<std::vector<Number>>& channels,
		const std::vector<std::vector<std::size_t>>& neighbours, const std::vector<bool>& goodMask = {}) {
		if (channels.empty()) {
			return {};
		}
		const std::size_t count = channels.size();
		const std::size_t samples = channels.front().size();
		for (const auto& channel : channels) {
			if (channel.size() != samples) {
				throw std::invalid_argument("laplacianReference: the channels differ in length");
			}
		}
		if (neighbours.size() != count) {
			throw std::invalid_argument("laplacianReference: the adjacency size does not match the channels");
		}
		if (!goodMask.empty() && goodMask.size() != count) {
			throw std::invalid_argument("laplacianReference: the mask size does not match the channels");
		}

		std::vector<bool> good(count, true);
		if (!goodMask.empty()) {
			good = goodMask;
		}

		std::vector<std::vector<Number>> result(channels);
		for (std::size_t index = 0; index < count; ++index) {
			if (!good[index]) {
				continue;
			}
			std::vector<std::size_t> usable;
			usable.reserve(neighbours[index].size());
			for (const std::size_t neighbour : neighbours[index]) {
				if (neighbour >= count) {
					throw std::invalid_argument("laplacianReference: a neighbour index is out of range");
				}
				if (good[neighbour]) {
					usable.push_back(neighbour);
				}
			}
			if (usable.empty()) {
				continue;
			}
			const Number divisor = static_cast<Number>(usable.size());
			for (std::size_t sample = 0; sample < samples; ++sample) {
				Number mean = Number(0);
				for (const std::size_t neighbour : usable) {
					mean += channels[neighbour][sample];
				}
				result[index][sample] -= mean / divisor;
			}
		}
		return result;
	}

	/// @brief  Builds a bipolar montage from channel pairs.
	/// @param  channels  Source montage.
	/// @param  pairs     (anode, cathode) index pairs.
	/// @return One row per pair, equal to anode minus cathode.
	/// @throws std::invalid_argument  When an index is out of range.
	template<typename Number = double>
	std::vector<std::vector<Number>> bipolarReference(const std::vector<std::vector<Number>>& channels,
		const std::vector<std::pair<std::size_t, std::size_t>>& pairs) {
		std::vector<std::vector<Number>> result;
		if (channels.empty() || pairs.empty()) {
			return result;
		}
		const std::size_t count = channels.size();
		const std::size_t samples = channels.front().size();
		for (const auto& channel : channels) {
			if (channel.size() != samples) {
				throw std::invalid_argument("bipolarReference: the channels differ in length");
			}
		}

		result.reserve(pairs.size());
		for (const auto& pair : pairs) {
			if (pair.first >= count || pair.second >= count) {
				throw std::invalid_argument("bipolarReference: a pair index is out of range");
			}
			std::vector<Number> row(samples);
			for (std::size_t sample = 0; sample < samples; ++sample) {
				row[sample] = channels[pair.first][sample] - channels[pair.second][sample];
			}
			result.push_back(std::move(row));
		}
		return result;
	}
}
