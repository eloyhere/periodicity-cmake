/// @file   hilbert.h
/// @brief  The analytic signal, envelope and instantaneous phase/frequency.
///
/// The analytic signal is computed with a direct discrete Fourier transform:
/// the negative-frequency bins are zeroed, the positive ones are doubled, and
/// the result is transformed back. That is O(n^2), which is the right trade for
/// the short windows these plots use and keeps the header free of an FFT.
///
/// This file is self-contained: it includes no other transform header.

#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace md::mathematic {

	/// @brief  The analytic signal x + j H[x] of a real sequence.
	/// @param  signal  Real input values.
	/// @return The complex analytic signal, same length as the input.
	/// @note   The discrete Hilbert transform pairs with the DFT: bin 0 and the
	///         Nyquist bin stay as they are, positive bins are doubled and
	///         negative bins are cleared.
	template<typename Real = double>
	std::vector<std::complex<Real>> analyticSignal(const std::vector<Real>& signal) {
		const std::size_t count = signal.size();
		if (count == 0) {
			return {};
		}

		const Real pi = static_cast<Real>(3.141592653589793238462643383279502884);
		std::vector<std::complex<Real>> spectrum(count);
		for (std::size_t bin = 0; bin < count; ++bin) {
			std::complex<Real> total(Real(0), Real(0));
			for (std::size_t index = 0; index < count; ++index) {
				const Real angle = -static_cast<Real>(2) * pi
					* static_cast<Real>((bin * index) % count) / static_cast<Real>(count);
				total += signal[index] * std::complex<Real>(static_cast<Real>(std::cos(angle)),
					static_cast<Real>(std::sin(angle)));
			}
			spectrum[bin] = total;
		}

		const std::size_t positiveEnd = (count % 2 == 0) ? count / 2 : (count + 1) / 2;
		for (std::size_t bin = 1; bin < positiveEnd; ++bin) {
			spectrum[bin] *= Real(2);
		}
		for (std::size_t bin = positiveEnd + ((count % 2 == 0) ? 1 : 0); bin < count; ++bin) {
			spectrum[bin] = std::complex<Real>(Real(0), Real(0));
		}

		std::vector<std::complex<Real>> result(count);
		for (std::size_t index = 0; index < count; ++index) {
			std::complex<Real> total(Real(0), Real(0));
			for (std::size_t bin = 0; bin < count; ++bin) {
				const Real angle = static_cast<Real>(2) * pi
					* static_cast<Real>((bin * index) % count) / static_cast<Real>(count);
				total += spectrum[bin] * std::complex<Real>(static_cast<Real>(std::cos(angle)),
					static_cast<Real>(std::sin(angle)));
			}
			result[index] = total / static_cast<Real>(count);
		}
		return result;
	}

	/// @brief  The instantaneous envelope |analytic(signal)|.
	template<typename Real = double>
	std::vector<Real> envelope(const std::vector<Real>& signal) {
		const std::vector<std::complex<Real>> analytic = analyticSignal(signal);
		std::vector<Real> result(analytic.size());
		for (std::size_t index = 0; index < analytic.size(); ++index) {
			result[index] = std::abs(analytic[index]);
		}
		return result;
	}

	/// @brief  The instantaneous phase arg(analytic(signal)), in radians.
	template<typename Real = double>
	std::vector<Real> instantaneousPhase(const std::vector<Real>& signal) {
		const std::vector<std::complex<Real>> analytic = analyticSignal(signal);
		std::vector<Real> result(analytic.size());
		for (std::size_t index = 0; index < analytic.size(); ++index) {
			result[index] = static_cast<Real>(std::arg(analytic[index]));
		}
		return result;
	}

	/// @brief  The instantaneous frequency in hertz.
	/// @param  signal      Real input values.
	/// @param  sampleRate  Sampling rate in hertz; must be positive.
	/// @return The wrapped phase difference times sampleRate / (2 pi); the
	///         first entry is zero because there is no preceding phase.
	/// @throws std::invalid_argument  When the sample rate is not positive.
	template<typename Real = double>
	std::vector<Real> instantaneousFrequency(const std::vector<Real>& signal,
		const Real& sampleRate) {
		if (!(sampleRate > Real(0))) {
			throw std::invalid_argument("instantaneousFrequency: the sample rate must be positive");
		}
		const std::vector<Real> phase = instantaneousPhase(signal);
		std::vector<Real> result(phase.size(), Real(0));
		const Real pi = static_cast<Real>(3.141592653589793238462643383279502884);
		for (std::size_t index = 1; index < phase.size(); ++index) {
			Real difference = phase[index] - phase[index - 1];
			while (difference > pi) {
				difference -= static_cast<Real>(2) * pi;
			}
			while (difference < -pi) {
				difference += static_cast<Real>(2) * pi;
			}
			result[index] = difference * sampleRate / (static_cast<Real>(2) * pi);
		}
		return result;
	}
}
