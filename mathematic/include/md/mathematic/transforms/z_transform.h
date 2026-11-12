/// @file   z_transform.h
/// @brief  The one-sided z transform of a finite sequence, its values on a
///         circle, and the inverse transform on that circle.
///
/// For a sequence x[0..N-1] the transform is X(z) = sum x[n] z^(-n). Sampling
/// it at N equally spaced points of a circle of radius r and applying the
/// inverse discrete Fourier transform recovers x[n] r^(-n), which is the
/// inverse transform of a finite causal sequence. This header is a small,
/// self-contained helper; the fast transform engine lives in sampler.h.
///
/// This file includes no other transform header.

#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace md::mathematic {

	/// @brief  The z transform of a finite sequence at one point.
	/// @param  samples  Sequence values.
	/// @param  z        Point in the z plane; must not be zero.
	/// @return sum over n of samples[n] z^(-n).
	/// @throws std::invalid_argument  When z is zero.
	template<typename Real = double>
	std::complex<Real> zTransform(const std::vector<Real>& samples, const std::complex<Real>& z) {
		if (z == std::complex<Real>(Real(0), Real(0))) {
			throw std::invalid_argument("zTransform: z must not be zero");
		}
		const std::complex<Real> inverse = Real(1) / z;
		std::complex<Real> power(Real(1), Real(0));
		std::complex<Real> total(Real(0), Real(0));
		for (const Real& sample : samples) {
			total += sample * power;
			power *= inverse;
		}
		return total;
	}

	/// @brief  The z transform sampled on a circle.
	/// @param  samples  Sequence values.
	/// @param  radius   Circle radius, positive.
	/// @param  angles   Angles in radians.
	/// @return One value per angle: X(radius * exp(j angle)).
	/// @throws std::invalid_argument  When the radius is not positive.
	template<typename Real = double>
	std::vector<std::complex<Real>> zTransformSpectrum(const std::vector<Real>& samples,
		const Real& radius, const std::vector<Real>& angles) {
		if (!(radius > Real(0))) {
			throw std::invalid_argument("zTransformSpectrum: the radius must be positive");
		}
		std::vector<std::complex<Real>> result;
		result.reserve(angles.size());
		for (const Real& angle : angles) {
			result.push_back(zTransform(samples,
				std::complex<Real>(radius * static_cast<Real>(std::cos(angle)),
					radius * static_cast<Real>(std::sin(angle)))));
		}
		return result;
	}

	/// @brief  Inverts a z transform sampled on a circle.
	/// @param  values  X(r exp(j 2 pi k / N)) for k = 0 .. N-1.
	/// @param  radius  Radius of the sampling circle, positive.
	/// @return The causal sequence x[0..N-1] recovered from the samples.
	/// @throws std::invalid_argument  When the radius is not positive.
	/// @note   The result assumes the sequence is causal and shorter than N.
	template<typename Real = double>
	std::vector<Real> inverseZTransform(const std::vector<std::complex<Real>>& values,
		const Real& radius) {
		if (!(radius > Real(0))) {
			throw std::invalid_argument("inverseZTransform: the radius must be positive");
		}
		const std::size_t count = values.size();
		if (count == 0) {
			return {};
		}
		const Real pi = static_cast<Real>(3.141592653589793238462643383279502884);
		std::vector<Real> result(count, Real(0));
		Real scale = Real(1);
		for (std::size_t index = 0; index < count; ++index) {
			std::complex<Real> total(Real(0), Real(0));
			for (std::size_t bin = 0; bin < count; ++bin) {
				const Real angle = static_cast<Real>(2) * pi
					* static_cast<Real>((bin * index) % count) / static_cast<Real>(count);
				total += values[bin] * std::complex<Real>(static_cast<Real>(std::cos(angle)),
					static_cast<Real>(std::sin(angle)));
			}
			result[index] = (total / static_cast<Real>(count)).real() * scale;
			scale *= radius;
		}
		return result;
	}
}
