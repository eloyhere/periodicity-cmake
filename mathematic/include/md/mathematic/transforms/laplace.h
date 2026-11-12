/// @file   laplace.h
/// @brief  The one-sided Laplace transform of a sampled signal, its numerical
///         inversion by the fixed Talbot contour, and the complex surface it
///         forms over a grid of sigma + j omega points.
///
/// The forward transform integrates f(t) exp(-s t) over the sampled interval
/// with an adaptive five-point Gauss-Legendre rule on every panel; the signal
/// is interpolated linearly between samples. A tail estimate extends the last
/// panel to infinity when the final samples decay without a phase flip, which
/// is what an exponentially decaying signal (or a damped oscillation) does.
///
/// The integral converges only to the right of the abscissa of convergence.
/// estimateLaplaceRegion() infers that abscissa from the sampled tail where it
/// can; laplaceSpectrum() reports it alongside the surface. On the boundary
/// itself the one-sided integral is not absolutely convergent, so a caller
/// that wants the analytic continuation should move a small distance to the
/// right of the reported abscissa.

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

	/// @brief  An estimate of the half-plane in which the Laplace integral
	///         converges.
	/// @tparam Real  Floating-point type used for the abscissa.
	template<typename Real = double>
	struct LaplaceRegion {
		Real abscissa = -std::numeric_limits<Real>::infinity();	///< Re(s) must exceed this.
		bool estimated = false;									///< True when the tail supplied the value.

		/// @brief  Tests whether a point lies inside the estimated region.
		/// @param  s  Complex frequency to test.
		/// @return True when no estimate was possible or Re(s) is to the right
		///         of the abscissa.
		bool contains(const std::complex<Real>& s) const {
			return !estimated || s.real() > abscissa;
		}
	};

	/// @brief  Tuning knobs for the forward Laplace integral.
	/// @tparam Real  Floating-point type used for the tolerances.
	template<typename Real = double>
	struct LaplaceOptions {
		Real relativeTolerance = static_cast<Real>(1.0e-12);	///< Per-panel Gauss-Legendre tolerance.
		std::size_t maxDepth = 8;								///< Maximum panel subdivisions.
		bool estimateTail = true;								///< Add the exponential tail estimate.
	};

	/// @brief  The complex Laplace surface over a sigma + j omega grid.
	/// @tparam Real  Floating-point type used for the grid and the values.
	template<typename Real = double>
	struct LaplaceSurface {
		std::vector<Real> sigmas;										///< Real parts, one per row.
		std::vector<Real> omegas;										///< Imaginary parts, one per column.
		std::vector<std::vector<std::complex<Real>>> values;			///< values[row][column].
		LaplaceRegion<Real> region;										///< Convergence estimate.

		/// @brief  One cell of the surface.
		/// @throws std::out_of_range  When a coordinate is outside the grid.
		const std::complex<Real>& at(const std::size_t row, const std::size_t column) const {
			return values.at(row).at(column);
		}

		/// @brief  Number of rows (sigmas).
		std::size_t rows() const {
			return values.size();
		}

		/// @brief  Number of columns (omegas).
		std::size_t columns() const {
			return values.empty() ? 0 : values.front().size();
		}
	};

	namespace laplace_detail {
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

		/// @brief  exp(-s t) for a complex s and a real t.
		template<typename Real>
		std::complex<Real> exponential(const std::complex<Real>& s, const Real& t) {
			const Real magnitude = static_cast<Real>(std::exp(-s.real() * t));
			const Real angle = s.imag() * t;
			return std::complex<Real>(magnitude * static_cast<Real>(std::cos(angle)),
				-magnitude * static_cast<Real>(std::sin(angle)));
		}

		/// @brief  Catmull-Rom interpolation inside one panel.
		/// @param  f     All samples.
		/// @param  left  Index of the left panel endpoint.
		/// @param  u     Position in the panel, in [0, 1].
		/// @return The interpolated value; linear at the two outer panels where
		///         a full four-point stencil does not exist.
		template<typename Real>
		std::complex<Real> interpolatePanel(const std::vector<std::complex<Real>>& f,
			const std::size_t left, const Real& u) {
			if (left == 0 || left + 2 >= f.size()) {
				return f[left] + (f[left + 1] - f[left]) * u;
			}
			const Real u2 = u * u;
			const Real u3 = u2 * u;
			const Real weight0 = static_cast<Real>(-0.5) * u3 + u2 - static_cast<Real>(0.5) * u;
			const Real weight1 = static_cast<Real>(1.5) * u3 - static_cast<Real>(2.5) * u2 + Real(1);
			const Real weight2 = static_cast<Real>(-1.5) * u3 + Real(2) * u2
				+ static_cast<Real>(0.5) * u;
			const Real weight3 = static_cast<Real>(0.5) * u3 - static_cast<Real>(0.5) * u2;
			return weight0 * f[left - 1] + weight1 * f[left] + weight2 * f[left + 1]
				+ weight3 * f[left + 2];
		}

		/// @brief  One five-point Gauss-Legendre pass over a segment of a panel.
		template<typename Real>
		std::complex<Real> gaussSegment(const std::vector<Real>& t,
			const std::vector<std::complex<Real>>& f, const std::size_t left,
			const Real& begin, const Real& end, const std::complex<Real>& s) {
			const Real halfWidth = (end - begin) / Real(2);
			const Real middle = (end + begin) / Real(2);
			const Real width = t[left + 1] - t[left];
			std::complex<Real> total(Real(0), Real(0));
			const std::vector<Real>& points = nodes<Real>();
			const std::vector<Real>& coefficients = weights<Real>();
			for (std::size_t index = 0; index < points.size(); ++index) {
				const Real time = middle + halfWidth * points[index];
				const Real u = (time - t[left]) / width;
				total += coefficients[index] * interpolatePanel(f, left, u) * exponential(s, time);
			}
			return total * halfWidth;
		}

		/// @brief  Adaptively refines one panel until two levels agree.
		template<typename Real>
		std::complex<Real> adaptiveSegment(const std::vector<Real>& t,
			const std::vector<std::complex<Real>>& f, const std::size_t left,
			const Real& begin, const Real& end, const std::complex<Real>& s,
			const LaplaceOptions<Real>& options, const std::size_t depth) {
			const std::complex<Real> whole = gaussSegment(t, f, left, begin, end, s);
			const Real middle = (begin + end) / Real(2);
			const std::complex<Real> halves = gaussSegment(t, f, left, begin, middle, s)
				+ gaussSegment(t, f, left, middle, end, s);
			if (depth >= options.maxDepth
				|| std::abs(halves - whole) <= options.relativeTolerance
					* std::max(std::abs(halves), Real(1))) {
				return halves;
			}
			return adaptiveSegment(t, f, left, begin, middle, s, options, depth + 1)
				+ adaptiveSegment(t, f, left, middle, end, s, options, depth + 1);
		}
	}

	/// @brief  Estimates the abscissa of convergence from the sampled tail.
	/// @param  t  Sample times, strictly increasing.
	/// @param  f  Sample values, one per time.
	/// @return The estimate, or a region with estimated == false when the tail
	///         is oscillatory, zero, or not decaying.
	/// @note   The estimate is ln|f(t_last)| / t_last, which is exact for a
	///         single exponential and only indicative for a sum of them.
	template<typename Real = double>
	LaplaceRegion<Real> estimateLaplaceRegion(const std::vector<Real>& t,
		const std::vector<std::complex<Real>>& f) {
		LaplaceRegion<Real> region;
		if (t.size() != f.size() || t.size() < 3 || !(t.back() > Real(0))) {
			return region;
		}
		const std::size_t last = f.size() - 1;
		const Real magnitude = std::abs(f[last]);
		if (!(magnitude > Real(0)) || !(std::abs(f[last - 1]) > Real(0))) {
			return region;
		}
		const std::complex<Real> ratio = f[last] / f[last - 1];
		if (!(ratio.real() > Real(0))) {
			return region;
		}
		region.abscissa = static_cast<Real>(std::log(magnitude)) / t[last];
		region.estimated = true;
		return region;
	}

	/// @brief  The one-sided Laplace transform at one complex frequency.
	/// @param  t        Sample times in seconds, strictly increasing.
	/// @param  f        Sample values, one per time.
	/// @param  s        Complex frequency at which to evaluate.
	/// @param  options  Integration tolerance and tail switch.
	/// @return The integral of f(t) exp(-s t) over [t.front(), infinity).
	/// @throws std::invalid_argument  When the arrays differ in size, fewer
	///         than two samples exist, or the times are not increasing.
	/// @note   The integral converges only for Re(s) inside the region of
	///         convergence; outside it the result is a finite truncated
	///         integral, not the analytic continuation.
	template<typename Real = double>
	std::complex<Real> laplace(const std::vector<Real>& t, const std::vector<std::complex<Real>>& f,
		const std::complex<Real>& s, const LaplaceOptions<Real>& options = LaplaceOptions<Real>{}) {
		if (t.size() != f.size()) {
			throw std::invalid_argument("laplace: the time and value arrays must have the same size");
		}
		if (t.size() < 2) {
			throw std::invalid_argument("laplace: at least two samples are required");
		}
		for (std::size_t index = 1; index < t.size(); ++index) {
			if (!(t[index] > t[index - 1])) {
				throw std::invalid_argument("laplace: the sample times must be strictly increasing");
			}
		}

		std::complex<Real> total(Real(0), Real(0));
		for (std::size_t index = 0; index + 1 < t.size(); ++index) {
			total += laplace_detail::adaptiveSegment(t, f, index, t[index], t[index + 1],
				s, options, 0);
		}

		// Tail: extrapolate the final exponential and integrate it analytically.
		if (options.estimateTail && t.size() >= 3) {
			const std::size_t last = t.size() - 1;
			const Real magnitude = std::abs(f[last]);
			const Real previousMagnitude = std::abs(f[last - 1]);
			if (magnitude > Real(0) && previousMagnitude > Real(0)) {
				const std::complex<Real> ratio = f[last] / f[last - 1];
				if (ratio.real() > Real(0)) {
					const Real width = t[last] - t[last - 1];
					const std::complex<Real> lambda = std::log(ratio) / width;
					const std::complex<Real> denominator = s - lambda;
					if (denominator.real() > Real(0)) {
						total += f[last] * laplace_detail::exponential(s, t[last]) / denominator;
					}
				}
			}
		}
		return total;
	}

	/// @brief  The Laplace surface over a grid of sigma + j omega points.
	/// @param  t        Sample times in seconds, strictly increasing.
	/// @param  f        Sample values, one per time.
	/// @param  sigmas   Real parts to evaluate.
	/// @param  omegas   Imaginary parts to evaluate.
	/// @param  options  Integration tolerance and tail switch.
	/// @return values[i][j] = laplace(t, f, sigmas[i] + j omegas[j]) and the
	///         convergence estimate for the sampled signal.
	/// @note   The surface evaluates one integral per cell. For a large grid,
	///         compute the rows you need instead of filling every cell.
	template<typename Real = double>
	LaplaceSurface<Real> laplaceSpectrum(const std::vector<Real>& t,
		const std::vector<std::complex<Real>>& f, const std::vector<Real>& sigmas,
		const std::vector<Real>& omegas,
		const LaplaceOptions<Real>& options = LaplaceOptions<Real>{}) {
		LaplaceSurface<Real> surface;
		surface.sigmas = sigmas;
		surface.omegas = omegas;
		surface.region = estimateLaplaceRegion(t, f);
		surface.values.resize(sigmas.size());
		for (std::size_t row = 0; row < sigmas.size(); ++row) {
			surface.values[row].resize(omegas.size());
			for (std::size_t column = 0; column < omegas.size(); ++column) {
				surface.values[row][column] = laplace(t, f,
					std::complex<Real>(sigmas[row], omegas[column]), options);
			}
		}
		return surface;
	}

	/// @brief  Inverts a Laplace transform with the fixed Talbot contour.
	/// @param  F       Callable: std::complex<Real> -> std::complex<Real>.
	/// @param  t       Time at which to evaluate, positive.
	/// @param  degree  Number of Talbot nodes; 24 is the accuracy sweet spot in
	///                 double precision, larger values trade roundoff for a
	///                 slightly different contour.
	/// @return The time-domain value; the imaginary part is near zero for a
	///         transform of a real signal.
	/// @throws std::invalid_argument  When t is not positive or degree < 8.
	/// @note   This is Talbot's contour deformed from the Bromwich line. In
	///         double precision the error is about 1e-11 at degree 24 for a
	///         smooth transform such as 1/(s+a) or a damped sinusoid; it grows
	///         if a singularity lies close to the contour or if degree is
	///         pushed much above 32.
	template<typename Function, typename Real = double>
	std::complex<Real> inverseLaplace(const Function& F, const Real& t,
		const std::size_t degree = 24) {
		if (!(t > Real(0))) {
			throw std::invalid_argument("inverseLaplace: the evaluation time must be positive");
		}
		if (degree < 8) {
			throw std::invalid_argument("inverseLaplace: at least eight Talbot nodes are required");
		}

		const std::size_t count = degree;
		const Real r = static_cast<Real>(0.4) * static_cast<Real>(count);
		const Real pi = static_cast<Real>(3.141592653589793238462643383279502884);
		std::vector<std::complex<Real>> parameters(count);
		std::vector<Real> cotangents(count, Real(0));
		parameters[0] = std::complex<Real>(r / t, Real(0));
		for (std::size_t index = 1; index < count; ++index) {
			const Real theta = pi * static_cast<Real>(index) / static_cast<Real>(count);
			const Real cotangent = static_cast<Real>(1) / static_cast<Real>(std::tan(theta));
			cotangents[index] = cotangent;
			parameters[index] = (r / t) * std::complex<Real>(theta * cotangent, theta);
		}

		std::complex<Real> total = std::exp(r) * static_cast<std::complex<Real>>(F(parameters[0])) / Real(2);
		for (std::size_t index = 1; index < count; ++index) {
			const Real theta = pi * static_cast<Real>(index) / static_cast<Real>(count);
			const Real cotangent = cotangents[index];
			const std::complex<Real> weight(Real(1), theta * (Real(1) + cotangent * cotangent) - cotangent);
			total += std::exp(t * parameters[index])
				* static_cast<std::complex<Real>>(F(parameters[index])) * weight;
		}
		return total * (static_cast<Real>(0.4) / t);
	}

	/// @brief  Inverts a Laplace transform at several times.
	/// @param  F       Callable: std::complex<Real> -> std::complex<Real>.
	/// @param  times   Positive evaluation times.
	/// @param  degree  Number of Talbot nodes per time.
	/// @return One value per time.
	template<typename Function, typename Real = double>
	std::vector<std::complex<Real>> inverseLaplace(const Function& F,
		const std::vector<Real>& times, const std::size_t degree = 24) {
		std::vector<std::complex<Real>> result;
		result.reserve(times.size());
		for (const Real& time : times) {
			result.push_back(inverseLaplace(F, time, degree));
		}
		return result;
	}
}
