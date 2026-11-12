/// @file   mellin.h
/// @brief  The Mellin transform of a sampled positive signal, its inverse by
///         a Bromwich contour, the surface over a c + j t grid, and the complex
///         gamma function that the exponential signal transforms to.
///
/// The forward integral is evaluated after the substitution x = exp(u):
///
///     M(s) = integral from 0 to infinity of f(x) x^(s-1) dx
///          = integral from -infinity to infinity of f(exp(u)) exp(s u) du.
///
/// That form removes the x^(s-1) endpoint behaviour from the quadrature: a
/// five-point Gauss-Legendre rule runs on each logarithmic panel and the
/// missing near-zero panel is integrated analytically as f(0) x0^s / s.
///
/// inverseMellin() evaluates the Bromwich integral
///
///     f(x) = (1 / (2 pi j)) * integral over c - j infinity .. c + j infinity
///            of F(s) x^(-s) ds
///
/// with the trapezoidal rule on a finite vertical segment. The rule converges
/// exponentially when F decays along the imaginary direction, which is the
/// case for the gamma function and for rational transforms; choose c inside
/// the strip of convergence and halfWidth large enough for that decay.

#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace md::mathematic {

	/// @brief  The complex surface of a Mellin transform over a c + j t grid.
	/// @tparam Real  Floating-point type used for the grid and the values.
	template<typename Real = double>
	struct MellinSurface {
		std::vector<Real> cs;										///< Real parts, one per row.
		std::vector<Real> ts;										///< Imaginary parts, one per column.
		std::vector<std::vector<std::complex<Real>>> values;		///< values[row][column].

		/// @brief  One cell of the surface.
		/// @throws std::out_of_range  When a coordinate is outside the grid.
		const std::complex<Real>& at(const std::size_t row, const std::size_t column) const {
			return values.at(row).at(column);
		}

		/// @brief  Number of rows (real parts).
		std::size_t rows() const {
			return values.size();
		}

		/// @brief  Number of columns (imaginary parts).
		std::size_t columns() const {
			return values.empty() ? 0 : values.front().size();
		}
	};

	namespace mellin_detail {
		/// @brief  The five-point Gauss-Legendre nodes on [-1, 1].
		template<typename Real>
		const std::vector<Real>& nodes() {
			static const std::vector<Real> table{
				static_cast<Real>(-0.9061798459386640),
				static_cast<Real>(-0.5384693101056831),
				static_cast<Real>(0),
				static_cast<Real>(0.5384693101056831),
				static_cast<Real>(0.9061798459386640)
			};
			return table;
		}

		/// @brief  The matching five-point Gauss-Legendre weights.
		template<typename Real>
		const std::vector<Real>& weights() {
			static const std::vector<Real> table{
				static_cast<Real>(0.2369268850561891),
				static_cast<Real>(0.4786286704993665),
				static_cast<Real>(0.5688888888888889),
				static_cast<Real>(0.4786286704993665),
				static_cast<Real>(0.2369268850561891)
			};
			return table;
		}

		/// @brief  Linear interpolation in the logarithmic variable.
		template<typename Real>
		std::complex<Real> interpolate(const std::vector<std::complex<Real>>& values,
			const std::vector<Real>& positions, const std::size_t left, const Real& u) {
			const Real width = positions[left + 1] - positions[left];
			if (!(width > Real(0))) {
				return values[left];
			}
			const Real fraction = (u - positions[left]) / width;
			return values[left] + (values[left + 1] - values[left]) * fraction;
		}

		/// @brief  One five-point Gauss-Legendre pass over a segment.
		template<typename Real>
		std::complex<Real> gaussSegment(const std::vector<std::complex<Real>>& values,
			const std::vector<Real>& positions, const std::size_t left,
			const Real& begin, const Real& end, const std::complex<Real>& s) {
			const Real halfWidth = (end - begin) / Real(2);
			const Real middle = (end + begin) / Real(2);
			const std::vector<Real>& points = nodes<Real>();
			const std::vector<Real>& coefficients = weights<Real>();
			std::complex<Real> total(Real(0), Real(0));
			for (std::size_t index = 0; index < points.size(); ++index) {
				const Real u = middle + halfWidth * points[index];
				const std::complex<Real> value = interpolate(values, positions, left, u);
				total += coefficients[index] * value * std::exp(s * u);
			}
			return total * halfWidth;
		}

		/// @brief  Adaptively refines one logarithmic panel.
		template<typename Real>
		std::complex<Real> adaptiveSegment(const std::vector<std::complex<Real>>& values,
			const std::vector<Real>& positions, const std::size_t left,
			const Real& begin, const Real& end, const std::complex<Real>& s,
			const std::size_t maxDepth, const Real& tolerance, const std::size_t depth) {
			const std::complex<Real> whole = gaussSegment(values, positions, left, begin, end, s);
			const Real middle = (begin + end) / Real(2);
			const std::complex<Real> halves = gaussSegment(values, positions, left, begin, middle, s)
				+ gaussSegment(values, positions, left, middle, end, s);
			if (depth >= maxDepth
				|| std::abs(halves - whole) <= tolerance * std::max(std::abs(halves), Real(1))) {
				return halves;
			}
			return adaptiveSegment(values, positions, left, begin, middle, s, maxDepth, tolerance,
				depth + 1)
				+ adaptiveSegment(values, positions, left, middle, end, s, maxDepth, tolerance,
					depth + 1);
		}
	}

	/// @brief  The complex gamma function, by the Lanczos approximation.
	/// @param  z  Complex argument.
	/// @return Gamma(z), reflection used when Re(z) < 0.5.
	/// @note   Double-precision coefficients; the relative error is near 1e-14
	///         on the positive real axis and grows only slowly off it.
	template<typename Real = double>
	std::complex<Real> complexGamma(const std::complex<Real>& z) {
		static const Real coefficients[9] = {
			static_cast<Real>(0.99999999999980993),
			static_cast<Real>(676.5203681218851),
			static_cast<Real>(-1259.1392167224028),
			static_cast<Real>(771.32342877765313),
			static_cast<Real>(-176.61502916214059),
			static_cast<Real>(12.507343278686905),
			static_cast<Real>(-0.13857109526572012),
			static_cast<Real>(9.9843695780195716e-6),
			static_cast<Real>(1.5056327351493116e-7)
		};
		const Real pi = static_cast<Real>(3.141592653589793238462643383279502884);
		if (z.real() < static_cast<Real>(0.5)) {
			const std::complex<Real> sine = std::sin(pi * z);
			if (std::abs(sine) == Real(0)) {
				throw std::domain_error("complexGamma: pole of the gamma function");
			}
			return pi / (sine * complexGamma(Real(1) - z));
		}

		const std::complex<Real> shifted = z - Real(1);
		std::complex<Real> series(coefficients[0], Real(0));
		for (std::size_t index = 1; index < 9; ++index) {
			series += coefficients[index] / (shifted + static_cast<Real>(index));
		}
		const std::complex<Real> base = shifted + static_cast<Real>(7.5);
		return static_cast<Real>(std::sqrt(2 * pi)) * std::pow(base, shifted + Real(0.5))
			* std::exp(-base) * series;
	}

	/// @brief  The Mellin transform of a sampled positive signal.
	/// @param  x  Sample positions, strictly increasing and positive.
	/// @param  f  Sample values, one per position.
	/// @param  s  Complex transform variable c + j t.
	/// @return The integral of f(x) x^(s-1) dx over (0, infinity), with the
	///         near-zero panel integrated analytically and no upper tail.
	/// @throws std::invalid_argument  When the arrays differ in size, a
	///         position is not positive, or the positions are not increasing.
	/// @note   The sampled range must reach far enough that the remaining tail
	///         is negligible (x_max about 40 is enough for exp(-x)).
	template<typename Real = double>
	std::complex<Real> mellin(const std::vector<Real>& x,
		const std::vector<std::complex<Real>>& f, const std::complex<Real>& s) {
		if (x.size() != f.size()) {
			throw std::invalid_argument("mellin: the position and value arrays must have the same size");
		}
		if (x.size() < 2) {
			throw std::invalid_argument("mellin: at least two samples are required");
		}
		std::vector<Real> positions(x.size());
		for (std::size_t index = 0; index < x.size(); ++index) {
			if (!(x[index] > Real(0))) {
				throw std::invalid_argument("mellin: the sample positions must be positive");
			}
			if (index > 0 && !(x[index] > x[index - 1])) {
				throw std::invalid_argument("mellin: the sample positions must be strictly increasing");
			}
			positions[index] = static_cast<Real>(std::log(x[index]));
		}

		std::complex<Real> total(Real(0), Real(0));
		for (std::size_t index = 0; index + 1 < x.size(); ++index) {
			total += mellin_detail::adaptiveSegment(f, positions, index, positions[index],
				positions[index + 1], s, 8, static_cast<Real>(1.0e-12), 0);
		}

		// The panel (0, x0) under the constant-f continuation of the first sample.
		if (s.real() > Real(0)) {
			total += f.front() * std::pow(std::complex<Real>(x.front(), Real(0)), s) / s;
		}
		return total;
	}

	/// @brief  The Mellin transform of a real sampled signal.
	template<typename Real = double>
	std::complex<Real> mellin(const std::vector<Real>& x, const std::vector<Real>& f,
		const std::complex<Real>& s) {
		if (x.size() != f.size()) {
			throw std::invalid_argument("mellin: the position and value arrays must have the same size");
		}
		std::vector<std::complex<Real>> values(f.size());
		for (std::size_t index = 0; index < f.size(); ++index) {
			values[index] = std::complex<Real>(f[index], Real(0));
		}
		return mellin(x, values, s);
	}

	/// @brief  The Mellin surface over a c + j t grid.
	/// @param  x  Sample positions, strictly increasing and positive.
	/// @param  f  Sample values, one per position.
	/// @param  cs Real parts to evaluate.
	/// @param  ts Imaginary parts to evaluate.
	/// @return values[i][j] = mellin(x, f, cs[i] + j ts[j]).
	template<typename Real = double>
	MellinSurface<Real> mellinSpectrum(const std::vector<Real>& x,
		const std::vector<std::complex<Real>>& f, const std::vector<Real>& cs,
		const std::vector<Real>& ts) {
		MellinSurface<Real> surface;
		surface.cs = cs;
		surface.ts = ts;
		surface.values.resize(cs.size());
		for (std::size_t row = 0; row < cs.size(); ++row) {
			surface.values[row].resize(ts.size());
			for (std::size_t column = 0; column < ts.size(); ++column) {
				surface.values[row][column] = mellin(x, f,
					std::complex<Real>(cs[row], ts[column]));
			}
		}
		return surface;
	}

	/// @brief  Inverts a Mellin transform by a Bromwich contour.
	/// @param  F          Callable: std::complex<Real> -> std::complex<Real>.
	/// @param  x          Positive evaluation point.
	/// @param  c          Real part of the contour; must lie in the strip of
	///                    convergence and to the right of the poles.
	/// @param  halfWidth  Half-length of the vertical contour.
	/// @param  samples    Number of trapezoidal nodes, at least three.
	/// @return The time-domain value; the imaginary part is near zero for a
	///         transform of a real signal.
	/// @throws std::invalid_argument  When x is not positive or the contour is
	///         degenerate.
	/// @note   The trapezoidal rule converges exponentially when F decays along
	///         the imaginary direction. For F = Gamma(s), c = 1 and a half-width
	///         of 40 give about 1e-10 or better for x in [0.25, 4].
	template<typename Function, typename Real = double>
	std::complex<Real> inverseMellin(const Function& F, const Real& x, const Real& c = Real(1),
		const Real& halfWidth = Real(40), const std::size_t samples = 2049) {
		if (!(x > Real(0))) {
			throw std::invalid_argument("inverseMellin: the evaluation point must be positive");
		}
		if (samples < 3 || !(halfWidth > Real(0))) {
			throw std::invalid_argument("inverseMellin: the contour needs at least three nodes");
		}

		const Real step = (Real(2) * halfWidth) / static_cast<Real>(samples - 1);
		const Real logx = static_cast<Real>(std::log(x));
		const std::complex<Real> scale = std::pow(std::complex<Real>(x, Real(0)), -c);
		std::complex<Real> total(Real(0), Real(0));
		for (std::size_t index = 0; index < samples; ++index) {
			const Real omega = -halfWidth + step * static_cast<Real>(index);
			const std::complex<Real> s(c, omega);
			const Real weight = (index == 0 || index + 1 == samples) ? Real(0.5) : Real(1);
			total += weight * static_cast<std::complex<Real>>(F(s))
				* std::exp(std::complex<Real>(Real(0), -omega * logx));
		}
		return total * scale * (step / (Real(2) * static_cast<Real>(3.141592653589793238462643383279502884)));
	}

	/// @brief  Inverts a Mellin transform at several positive points.
	template<typename Function, typename Real = double>
	std::vector<std::complex<Real>> inverseMellin(const Function& F,
		const std::vector<Real>& xs, const Real& c = Real(1), const Real& halfWidth = Real(40),
		const std::size_t samples = 2049) {
		std::vector<std::complex<Real>> result;
		result.reserve(xs.size());
		for (const Real& x : xs) {
			result.push_back(inverseMellin(F, x, c, halfWidth, samples));
		}
		return result;
	}
}
