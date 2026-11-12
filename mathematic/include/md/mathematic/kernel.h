/// @file   kernel.h
/// @brief  The kernel catalogue: the radial shapes used by interpolation and
///         Gaussian-process style regression, and the inner-product kernels used
///         by kernel machines.
///
/// Every kernel has exactly one definition, here. Anything else in the library
/// that needs a kernel - the thin-plate spline in topography.h, for instance -
/// delegates to this header instead of restating the formula.
///
/// Two conventions are worth stating up front:
///  - A radial kernel takes the Euclidean distance r >= 0 and is scaled by a
///    length scale l > 0.
///  - The bounded kernels satisfy k(0) = 1. The conditionally positive definite
///    ones (thin-plate spline, cubic, linear) satisfy k(0) = 0 instead, because
///    they are used together with a polynomial term, not as a correlation.

#pragma once

#include <md/mathematic/matrix.h>
#include <md/mathematic/numerical.h>
#include <md/mathematic/window.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace md::mathematic {

	/// @brief  The available kernel shapes.
	enum class KernelType {
		Gaussian,				///< exp(-r^2 / (2 l^2)); infinitely smooth.
		Exponential,			///< exp(-r / l); the Ornstein-Uhlenbeck kernel.
		Matern12,				///< Same as Exponential, named for the Matern family.
		Matern32,				///< Once differentiable; the usual GP default.
		Matern52,				///< Twice differentiable.
		RationalQuadratic,		///< A scale mixture of Gaussians.
		Cauchy,					///< Heavy tails.
		Multiquadric,			///< sqrt(r^2 + l^2); grows with distance.
		InverseMultiquadric,	///< 1 / sqrt(r^2 + l^2); heavy tails.
		ThinPlateSpline,		///< r^2 log r; the interpolation kernel of topography.h.
		Cubic,					///< r^3.
		LinearRadial,			///< r.
		Linear,					///< Inner product x . y.
		Polynomial,				///< (gamma x . y + c)^d.
		Sigmoid,				///< tanh(gamma x . y + c).
		Cosine					///< Normalised inner product.
	};

	/// @brief  Shape parameters shared by the kernels.
	///
	/// Only the fields a given kernel reads are meaningful; the rest keep their
	/// defaults. Validation happens in the kernel that uses a field, so a bad
	/// degree does not stop a Gaussian from being evaluated.
	///
	/// @tparam Number  Floating-point type used for the parameters.
	template<typename Number = double>
	struct KernelParameters {
		Number lengthScale = static_cast<Number>(1);	///< l, must be positive.
		Number alpha = static_cast<Number>(1);			///< Rational-quadratic shape.
		Number degree = static_cast<Number>(3);			///< Polynomial degree.
		Number offset = static_cast<Number>(0);			///< Polynomial and sigmoid constant c.
		Number gamma = static_cast<Number>(1);			///< Polynomial and sigmoid slope.
	};

	/// @brief  Evaluates one radial kernel.
	/// @param  type        Shape to evaluate.
	/// @param  radius      Euclidean distance, at least zero.
	/// @param  parameters  Length scale and shape parameters.
	/// @return k(r).
	/// @throws std::invalid_argument  When the length scale is not positive or a
	///         shape parameter that the kernel reads is out of range.
	template<typename Number = double>
	Number radialKernel(const KernelType type, const Number& radius,
		const KernelParameters<Number>& parameters = KernelParameters<Number>{}) {
		if (radius < Number(0)) {
			throw std::invalid_argument("radialKernel: the radius must not be negative");
		}

		const Number length = parameters.lengthScale;
		const bool needsLength = type == KernelType::Gaussian || type == KernelType::Exponential
			|| type == KernelType::Matern12 || type == KernelType::Matern32
			|| type == KernelType::Matern52 || type == KernelType::RationalQuadratic
			|| type == KernelType::Cauchy || type == KernelType::Multiquadric
			|| type == KernelType::InverseMultiquadric || type == KernelType::ThinPlateSpline;
		if (needsLength && !(length > Number(0))) {
			throw std::invalid_argument("radialKernel: the length scale must be positive");
		}

		const Number scaled = (needsLength && length > Number(0)) ? radius / length : radius;

		switch (type) {
		case KernelType::Gaussian:
			return static_cast<Number>(std::exp(-Number(0.5) * scaled * scaled));

		case KernelType::Exponential:
		case KernelType::Matern12:
			return static_cast<Number>(std::exp(-scaled));

		case KernelType::Matern32: {
			const Number root = static_cast<Number>(std::sqrt(Number(3))) * scaled;
			return (Number(1) + root) * static_cast<Number>(std::exp(-root));
		}

		case KernelType::Matern52: {
			const Number root = static_cast<Number>(std::sqrt(Number(5))) * scaled;
			return (Number(1) + root + root * root / Number(3)) * static_cast<Number>(std::exp(-root));
		}

		case KernelType::RationalQuadratic: {
			if (!(parameters.alpha > Number(0))) {
				throw std::invalid_argument("radialKernel: rational quadratic needs a positive alpha");
			}
			const Number base = Number(1) + scaled * scaled / (Number(2) * parameters.alpha);
			return static_cast<Number>(std::pow(base, -parameters.alpha));
		}

		case KernelType::Cauchy:
			return Number(1) / (Number(1) + scaled * scaled);

		case KernelType::Multiquadric:
			return static_cast<Number>(std::sqrt(scaled * scaled + Number(1)));

		case KernelType::InverseMultiquadric:
			return Number(1) / static_cast<Number>(std::sqrt(scaled * scaled + Number(1)));

		case KernelType::ThinPlateSpline:
			// r^2 log r vanishes at the origin by its limit, not by a special case
			// chosen for convenience.
			return (radius <= Number(0)) ? Number(0)
				: scaled * scaled * static_cast<Number>(std::log(scaled));

		case KernelType::Cubic:
			return radius * radius * radius;

		case KernelType::LinearRadial:
			return radius;

		default:
			throw std::invalid_argument("radialKernel: this kernel is not radial");
		}
	}

	/// @brief  The Euclidean distance between two equally long vectors.
	/// @throws std::invalid_argument  When the lengths differ.
	template<typename Number = double>
	Number euclideanDistance(const std::vector<Number>& first, const std::vector<Number>& second) {
		if (first.size() != second.size()) {
			throw std::invalid_argument("euclideanDistance: the vectors differ in length");
		}
		Number total = Number(0);
		for (std::size_t index = 0; index < first.size(); ++index) {
			const Number difference = first[index] - second[index];
			total += difference * difference;
		}
		return static_cast<Number>(std::sqrt(total));
	}

	/// @brief  The inner product of two equally long vectors.
	/// @throws std::invalid_argument  When the lengths differ.
	template<typename Number = double>
	Number innerProduct(const std::vector<Number>& first, const std::vector<Number>& second) {
		if (first.size() != second.size()) {
			throw std::invalid_argument("innerProduct: the vectors differ in length");
		}
		Number total = Number(0);
		for (std::size_t index = 0; index < first.size(); ++index) {
			total += first[index] * second[index];
		}
		return total;
	}

	/// @brief  Evaluates any kernel on two feature vectors.
	/// @param  type        Shape to evaluate.
	/// @param  first       First feature vector.
	/// @param  second      Second feature vector, equally long.
	/// @param  parameters  Shape parameters.
	/// @return k(x, y).
	/// @throws std::invalid_argument  When the lengths differ or a parameter is bad.
	/// @note   Radial types are evaluated on the Euclidean distance; Linear,
	///         Polynomial, Sigmoid and Cosine on the inner product.
	template<typename Number = double>
	Number kernelValue(const KernelType type, const std::vector<Number>& first,
		const std::vector<Number>& second, const KernelParameters<Number>& parameters = KernelParameters<Number>{}) {
		switch (type) {
		case KernelType::Linear:
			return innerProduct(first, second);

		case KernelType::Polynomial: {
			if (!(parameters.degree >= Number(1))) {
				throw std::invalid_argument("kernelValue: the polynomial degree must be at least one");
			}
			const Number base = parameters.gamma * innerProduct(first, second) + parameters.offset;
			return static_cast<Number>(std::pow(base, parameters.degree));
		}

		case KernelType::Sigmoid:
			return static_cast<Number>(std::tanh(parameters.gamma * innerProduct(first, second)
				+ parameters.offset));

		case KernelType::Cosine: {
			const Number product = innerProduct(first, second);
			Number firstNorm = Number(0);
			Number secondNorm = Number(0);
			for (const Number& value : first) {
				firstNorm += value * value;
			}
			for (const Number& value : second) {
				secondNorm += value * value;
			}
			if (!(firstNorm > Number(0)) || !(secondNorm > Number(0))) {
				return Number(0);
			}
			return product / static_cast<Number>(std::sqrt(firstNorm * secondNorm));
		}

		default:
			return radialKernel(type, euclideanDistance(first, second), parameters);
		}
	}

	/// @brief  Builds the Gram matrix of a kernel over a point set.
	/// @param  type        Kernel shape.
	/// @param  points      One feature vector per point; the Gram matrix is n x n.
	/// @param  parameters  Shape parameters.
	/// @return K with K(i, j) = k(x_i, x_j).
	/// @throws std::invalid_argument  When the points differ in length.
	template<typename Number = double>
	Matrix<Number> gramMatrix(const KernelType type, const std::vector<std::vector<Number>>& points,
		const KernelParameters<Number>& parameters = KernelParameters<Number>{}) {
		const std::size_t count = points.size();
		if (count == 0) {
			return Matrix<Number>();
		}
		for (const auto& point : points) {
			if (point.size() != points.front().size()) {
				throw std::invalid_argument("gramMatrix: the points differ in length");
			}
		}

		Matrix<Number> gram(count, count, Number(0));
		for (std::size_t row = 0; row < count; ++row) {
			for (std::size_t column = row; column < count; ++column) {
				const Number value = kernelValue(type, points[row], points[column], parameters);
				gram(row, column) = value;
				gram(column, row) = value;
			}
		}
		return gram;
	}

	/// @brief  A Gaussian (squared exponential) kernel.
	template<typename Number = double>
	Number gaussianKernel(const Number& radius, const Number& lengthScale = static_cast<Number>(1)) {
		KernelParameters<Number> parameters;
		parameters.lengthScale = lengthScale;
		return radialKernel(KernelType::Gaussian, radius, parameters);
	}

	/// @brief  An exponential kernel.
	template<typename Number = double>
	Number exponentialKernel(const Number& radius, const Number& lengthScale = static_cast<Number>(1)) {
		KernelParameters<Number> parameters;
		parameters.lengthScale = lengthScale;
		return radialKernel(KernelType::Exponential, radius, parameters);
	}

	/// @brief  A Matern kernel of order 3/2.
	template<typename Number = double>
	Number matern32Kernel(const Number& radius, const Number& lengthScale = static_cast<Number>(1)) {
		KernelParameters<Number> parameters;
		parameters.lengthScale = lengthScale;
		return radialKernel(KernelType::Matern32, radius, parameters);
	}

	/// @brief  A Matern kernel of order 5/2.
	template<typename Number = double>
	Number matern52Kernel(const Number& radius, const Number& lengthScale = static_cast<Number>(1)) {
		KernelParameters<Number> parameters;
		parameters.lengthScale = lengthScale;
		return radialKernel(KernelType::Matern52, radius, parameters);
	}

	/// @brief  The thin-plate spline kernel r^2 log r.
	template<typename Number = double>
	Number thinPlateSplineKernel(const Number& radius, const Number& lengthScale = static_cast<Number>(1)) {
		KernelParameters<Number> parameters;
		parameters.lengthScale = lengthScale;
		return radialKernel(KernelType::ThinPlateSpline, radius, parameters);
	}

	/// @brief  The normalised sinc function, sin(pi x) / (pi x).
	/// @param  x  Argument, in samples.
	/// @return One at the origin and zero at every other integer.
	template<typename Number = double>
	Number sinc(const Number& x) {
		if (std::abs(x) < static_cast<Number>(1.0e-12)) {
			return Number(1);
		}
		const Number argument = Pi<Number> * x;
		return static_cast<Number>(std::sin(argument)) / argument;
	}

	/// @brief  The interpolation kernels used by the resampling routines.
	///
	/// Distinct from KernelType, which names the radial and inner-product
	/// kernels of the regression catalogue. A KernelId is a reconstruction
	/// kernel: its argument is a distance in samples, its support is finite,
	/// and the interpolating members satisfy k(0) = 1 and k(n) = 0 for every
	/// other integer n.
	enum class KernelId {
		Nearest,			///< Zero-order hold; support 0.5.
		Linear,				///< Triangle; support 1.
		CubicHermite,		///< Cubic with zero slopes at the ends; support 1.
		CatmullRom,			///< Cubic convolution with a = -0.5; support 2.
		BSpline,			///< Cubic B-spline, the B = 1, C = 0 cubic; support 2.
		Lanczos2,			///< sinc(x) sinc(x / 2); support 2.
		Lanczos3,			///< sinc(x) sinc(x / 3); support 3.
		BlackmanSinc,		///< Blackman-windowed sinc; support 6.
		WindowedSinc		///< Hann-windowed sinc; support 4.
	};

	/// @brief  The radius beyond which a reconstruction kernel is zero.
	/// @param  kernel  Kernel to query.
	/// @return The support radius in samples.
	inline double kernelSupport(const KernelId kernel) {
		switch (kernel) {
		case KernelId::Nearest:
			return 0.5;
		case KernelId::Linear:
		case KernelId::CubicHermite:
			return 1.0;
		case KernelId::CatmullRom:
		case KernelId::BSpline:
		case KernelId::Lanczos2:
			return 2.0;
		case KernelId::Lanczos3:
			return 3.0;
		case KernelId::BlackmanSinc:
			return 6.0;
		case KernelId::WindowedSinc:
			return 4.0;
		}
		return 0.0;
	}

	/// @brief  Evaluates a reconstruction kernel at a distance in samples.
	/// @param  kernel  Kernel shape.
	/// @param  x       Distance from the sample, in samples; may be negative.
	/// @return The kernel weight, zero outside kernelSupport(kernel).
	/// @note   BSpline is not interpolating: its value at the origin is 2/3.
	///         The sampler short-circuits exact grid positions so that asking
	///         for an existing sample still returns that sample exactly.
	template<typename Number = double>
	Number kernelValue(const KernelId kernel, const Number& x) {
		const Number distance = std::abs(x);
		const Number support = static_cast<Number>(kernelSupport(kernel));
		if (!(distance < support)) {
			return Number(0);
		}

		switch (kernel) {
		case KernelId::Nearest:
			return Number(1);

		case KernelId::Linear:
			return Number(1) - distance;

		case KernelId::CubicHermite: {
			// Smoothstep: value one at zero, zero with zero slope at one.
			return (Number(2) * distance - Number(3)) * distance * distance + Number(1);
		}

		case KernelId::CatmullRom: {
			if (distance < Number(1)) {
				return (Number(1.5) * distance - Number(2.5)) * distance * distance + Number(1);
			}
			return ((Number(-0.5) * distance + Number(2.5)) * distance - Number(4)) * distance
				+ Number(2);
		}

		case KernelId::BSpline: {
			if (distance < Number(1)) {
				return (Number(0.5) * distance - Number(1)) * distance * distance
					+ static_cast<Number>(2.0 / 3.0);
			}
			const Number remaining = Number(2) - distance;
			return remaining * remaining * remaining / Number(6);
		}

		case KernelId::Lanczos2:
			return sinc<Number>(distance) * sinc<Number>(distance / Number(2));

		case KernelId::Lanczos3:
			return sinc<Number>(distance) * sinc<Number>(distance / Number(3));

		case KernelId::BlackmanSinc:
			return sinc<Number>(distance)
				* static_cast<Number>(windowPoint(WindowId::Blackman,
					static_cast<double>(Number(0.5) + Number(0.5) * distance / support)));

		case KernelId::WindowedSinc:
			return sinc<Number>(distance)
				* static_cast<Number>(windowPoint(WindowId::Hann,
					static_cast<double>(Number(0.5) + Number(0.5) * distance / support)));
		}
		return Number(0);
	}
}
