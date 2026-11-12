/// @file   window.h
/// @brief  The classical window functions, their parameters, and the two gains
///         that decide how a window changes a spectrum.
///
/// Every window is written in the symmetric convention: for @c N points the
/// argument is 2*pi*n/(N-1) with n = 0 .. N-1, so w[0] and w[N-1] are the two
/// endpoints. Setting `periodic` switches to the DFT-even convention, which uses
/// N as the denominator instead; that is the right choice when the window is
/// applied before an FFT, because a symmetric Hann does not sum to a constant
/// across the periodic extension and leaks at the seam.

#pragma once

#include <md/mathematic/numerical.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace md::mathematic {

	/// @brief  The available window shapes.
	enum class WindowType {
		Rectangular,		///< All ones; no tapering.
		Hann,				///< Raised cosine; the default for general analysis.
		Hamming,			///< Raised cosine with a non-zero pedestal.
		Blackman,			///< Three-term cosine.
		BlackmanHarris,		///< Four-term cosine, very low sidelobes.
		Nuttall,			///< Three-term, continuous first derivative.
		FlatTop,			///< Wide and flat; best amplitude accuracy, poor resolution.
		Bartlett,			///< Triangular.
		BartlettHann,		///< Triangular blended with a raised cosine.
		Bohman,				///< Cosine plus a linear term; continuous derivative.
		Welch,				///< Parabolic.
		Cosine,				///< Half sine.
		Lanczos,			///< Sinc of a linear ramp.
		Tukey,				///< Rectangular core with cosine tapers.
		Gaussian,			///< Gaussian bell.
		Kaiser				///< Near-optimal time-bandwidth product.
	};

	/// @brief  Shape parameters and the sampling convention.
	///
	/// @tparam Number  Floating-point type used for the entries.
	template<typename Number = double>
	struct WindowParameters {
		Number alpha = static_cast<Number>(0.5);		///< Tukey taper fraction, in [0, 1].
		Number beta = static_cast<Number>(8.6);			///< Kaiser shape; 0 gives a rectangle.
		Number sigma = static_cast<Number>(0.4);		///< Gaussian width as a fraction of half-length.
		Number attenuation = static_cast<Number>(60);	///< Dolph-Chebyshev sidelobe level in dB.
		bool periodic = false;							///< Use N instead of N-1 as the denominator.
	};

	/// @brief  The modified Bessel function of the first kind, order zero.
	/// @param  x  Argument.
	/// @return I0(x), evaluated by its power series.
	/// @note   The series converges quickly for the range Kaiser windows use
	///         (beta at most about 20) and needs no special function library.
	template<typename Number = double>
	Number besselI0(const Number& x) {
		const Number quarter = x * x / Number(4);
		Number term = Number(1);
		Number total = Number(1);
		for (unsigned index = 1; index < 64; ++index) {
			term *= quarter / (static_cast<Number>(index) * static_cast<Number>(index));
			total += term;
			if (term < total * static_cast<Number>(1e-17)) {
				break;
			}
		}
		return total;
	}

	/// @brief  Evaluates the Chebyshev polynomial of the first kind.
	/// @param  degree  Polynomial degree, at least zero.
	/// @param  x       Argument; values outside [-1, 1] are handled by the cosh form.
	template<typename Number = double>
	Number chebyshev(const unsigned degree, const Number& x) {
		if (x > Number(1)) {
			return static_cast<Number>(std::cosh(static_cast<Number>(degree)
				* static_cast<Number>(std::acosh(x))));
		}
		if (x < Number(-1)) {
			const Number magnitude = static_cast<Number>(std::cosh(static_cast<Number>(degree)
				* static_cast<Number>(std::acosh(-x))));
			return (degree % 2 == 0) ? magnitude : -magnitude;
		}
		return static_cast<Number>(std::cos(static_cast<Number>(degree)
			* static_cast<Number>(std::acos(x))));
	}

	/// @brief  Builds one window.
	/// @param  type        Shape to generate.
	/// @param  size        Number of points; zero returns an empty vector.
	/// @param  parameters  Shape parameters and the sampling convention.
	/// @return The window, normalised so its maximum is one except for FlatTop.
	/// @throws std::invalid_argument  When Tukey alpha or Gaussian sigma is out of range.
	/// @note   Definitions, with phi = 2*pi*n/(N-1) and n = 0 .. N-1:
	///         - Hann: 0.5 - 0.5 cos(phi)
	///         - Hamming: 0.54 - 0.46 cos(phi)
	///         - Blackman: 0.42 - 0.5 cos(phi) + 0.08 cos(2 phi)
	///         - Blackman-Harris: 0.35875 - 0.48829 cos(phi) + 0.14128 cos(2 phi)
	///           - 0.01168 cos(3 phi)
	///         - Nuttall: 0.375 - 0.5 cos(phi) + 0.125 cos(2 phi)
	///         - Flat-top: 0.21557895 - 0.41663158 cos(phi) + 0.277263158 cos(2 phi)
	///           - 0.083578947 cos(3 phi) + 0.006947368 cos(4 phi)
	template<typename Number = double>
	std::vector<Number> makeWindow(const WindowType type, const std::size_t size,
		const WindowParameters<Number>& parameters = WindowParameters<Number>{}) {
		std::vector<Number> values(size, Number(0));
		if (size == 0) {
			return values;
		}
		if (size == 1) {
			values[0] = Number(1);
			return values;
		}

		const Number denominator = static_cast<Number>(parameters.periodic ? size : size - 1);
		const Number middle = (static_cast<Number>(size) - Number(1)) / Number(2);

		if (type == WindowType::Tukey && (parameters.alpha < Number(0) || parameters.alpha > Number(1))) {
			throw std::invalid_argument("makeWindow: Tukey alpha must lie in [0, 1]");
		}
		if (type == WindowType::Gaussian && !(parameters.sigma > Number(0))) {
			throw std::invalid_argument("makeWindow: Gaussian sigma must be positive");
		}

		const Number pi = Pi<Number>;
		const Number twoPi = DoublePi<Number>;

		switch (type) {
		case WindowType::Rectangular:
			std::fill(values.begin(), values.end(), Number(1));
			break;

		case WindowType::Hann:
			for (std::size_t index = 0; index < size; ++index) {
				const Number phi = twoPi * static_cast<Number>(index) / denominator;
				values[index] = Number(0.5) - Number(0.5) * static_cast<Number>(std::cos(phi));
			}
			break;

		case WindowType::Hamming:
			for (std::size_t index = 0; index < size; ++index) {
				const Number phi = twoPi * static_cast<Number>(index) / denominator;
				values[index] = Number(0.54) - Number(0.46) * static_cast<Number>(std::cos(phi));
			}
			break;

		case WindowType::Blackman:
			for (std::size_t index = 0; index < size; ++index) {
				const Number phi = twoPi * static_cast<Number>(index) / denominator;
				values[index] = Number(0.42) - Number(0.5) * static_cast<Number>(std::cos(phi))
					+ Number(0.08) * static_cast<Number>(std::cos(Number(2) * phi));
			}
			break;

		case WindowType::BlackmanHarris:
			for (std::size_t index = 0; index < size; ++index) {
				const Number phi = twoPi * static_cast<Number>(index) / denominator;
				values[index] = Number(0.35875) - Number(0.48829) * static_cast<Number>(std::cos(phi))
					+ Number(0.14128) * static_cast<Number>(std::cos(Number(2) * phi))
					- Number(0.01168) * static_cast<Number>(std::cos(Number(3) * phi));
			}
			break;

		case WindowType::Nuttall:
			for (std::size_t index = 0; index < size; ++index) {
				const Number phi = twoPi * static_cast<Number>(index) / denominator;
				values[index] = Number(0.375) - Number(0.5) * static_cast<Number>(std::cos(phi))
					+ Number(0.125) * static_cast<Number>(std::cos(Number(2) * phi));
			}
			break;

		case WindowType::FlatTop:
			for (std::size_t index = 0; index < size; ++index) {
				const Number phi = twoPi * static_cast<Number>(index) / denominator;
				values[index] = Number(0.21557895) - Number(0.41663158) * static_cast<Number>(std::cos(phi))
					+ Number(0.277263158) * static_cast<Number>(std::cos(Number(2) * phi))
					- Number(0.083578947) * static_cast<Number>(std::cos(Number(3) * phi))
					+ Number(0.006947368) * static_cast<Number>(std::cos(Number(4) * phi));
			}
			break;

		case WindowType::Bartlett:
			for (std::size_t index = 0; index < size; ++index) {
				const Number position = static_cast<Number>(index) / denominator;
				values[index] = Number(1) - std::abs(Number(2) * position - Number(1));
			}
			break;

		case WindowType::BartlettHann:
			for (std::size_t index = 0; index < size; ++index) {
				const Number position = static_cast<Number>(index) / denominator;
				const Number phi = twoPi * position;
				values[index] = Number(0.62) - Number(0.48) * std::abs(position - Number(0.5))
					- Number(0.38) * static_cast<Number>(std::cos(phi));
			}
			break;

		case WindowType::Bohman:
			for (std::size_t index = 0; index < size; ++index) {
				const Number x = std::abs(Number(2) * static_cast<Number>(index) / denominator - Number(1));
				values[index] = (Number(1) - x) * static_cast<Number>(std::cos(pi * x))
					+ static_cast<Number>(std::sin(pi * x)) / pi;
			}
			break;

		case WindowType::Welch:
			for (std::size_t index = 0; index < size; ++index) {
				const Number x = (static_cast<Number>(index) - middle) / middle;
				values[index] = Number(1) - x * x;
			}
			break;

		case WindowType::Cosine:
			for (std::size_t index = 0; index < size; ++index) {
				values[index] = static_cast<Number>(std::sin(pi * static_cast<Number>(index) / denominator));
			}
			break;

		case WindowType::Lanczos: {
			for (std::size_t index = 0; index < size; ++index) {
				const Number x = Number(2) * static_cast<Number>(index) / denominator - Number(1);
				values[index] = (x == Number(0))
					? Number(1)
					: static_cast<Number>(std::sin(pi * x)) / (pi * x);
			}
			break;
		}

		case WindowType::Tukey: {
			const Number taper = parameters.alpha;
			for (std::size_t index = 0; index < size; ++index) {
				const Number position = static_cast<Number>(index) / denominator;
				if (position < taper / Number(2)) {
					values[index] = Number(0.5) * (Number(1) + static_cast<Number>(std::cos(
						pi * (Number(2) * position / taper - Number(1)))));
				}
				else if (position > Number(1) - taper / Number(2)) {
					values[index] = Number(0.5) * (Number(1) + static_cast<Number>(std::cos(
						pi * (Number(2) * position / taper - Number(2) / taper + Number(1)))));
				}
				else {
					values[index] = Number(1);
				}
			}
			break;
		}

		case WindowType::Gaussian:
			for (std::size_t index = 0; index < size; ++index) {
				const Number x = (static_cast<Number>(index) - middle) / (parameters.sigma * middle);
				values[index] = static_cast<Number>(std::exp(-Number(0.5) * x * x));
			}
			break;

		case WindowType::Kaiser: {
			const Number normaliser = besselI0(parameters.beta);
			for (std::size_t index = 0; index < size; ++index) {
				const Number x = Number(2) * static_cast<Number>(index) / denominator - Number(1);
				values[index] = besselI0(parameters.beta
					* static_cast<Number>(std::sqrt(std::max(Number(1) - x * x, Number(0))))) / normaliser;
			}
			break;
		}
		}
		return values;
	}

	/// @brief  A Hann window.
	template<typename Number = double>
	std::vector<Number> hann(const std::size_t size, const bool periodic = false) {
		WindowParameters<Number> parameters;
		parameters.periodic = periodic;
		return makeWindow(WindowType::Hann, size, parameters);
	}

	/// @brief  A Hamming window.
	template<typename Number = double>
	std::vector<Number> hamming(const std::size_t size, const bool periodic = false) {
		WindowParameters<Number> parameters;
		parameters.periodic = periodic;
		return makeWindow(WindowType::Hamming, size, parameters);
	}

	/// @brief  A Blackman window.
	template<typename Number = double>
	std::vector<Number> blackman(const std::size_t size, const bool periodic = false) {
		WindowParameters<Number> parameters;
		parameters.periodic = periodic;
		return makeWindow(WindowType::Blackman, size, parameters);
	}

	/// @brief  A Tukey window with a given taper fraction.
	template<typename Number = double>
	std::vector<Number> tukey(const std::size_t size, const Number& alpha = static_cast<Number>(0.5),
		const bool periodic = false) {
		WindowParameters<Number> parameters;
		parameters.alpha = alpha;
		parameters.periodic = periodic;
		return makeWindow(WindowType::Tukey, size, parameters);
	}

	/// @brief  A Gaussian window of a given relative width.
	template<typename Number = double>
	std::vector<Number> gaussian(const std::size_t size, const Number& sigma = static_cast<Number>(0.4),
		const bool periodic = false) {
		WindowParameters<Number> parameters;
		parameters.sigma = sigma;
		parameters.periodic = periodic;
		return makeWindow(WindowType::Gaussian, size, parameters);
	}

	/// @brief  A Kaiser window of a given shape parameter.
	template<typename Number = double>
	std::vector<Number> kaiser(const std::size_t size, const Number& beta = static_cast<Number>(8.6),
		const bool periodic = false) {
		WindowParameters<Number> parameters;
		parameters.beta = beta;
		parameters.periodic = periodic;
		return makeWindow(WindowType::Kaiser, size, parameters);
	}

	/// @brief  The coherent gain: the mean of the window.
	/// @param  window  Window values.
	/// @return sum(w)/N, the factor by which a window scales a sinusoid's amplitude.
	/// @throws std::invalid_argument  When the window is empty.
	template<typename Number = double>
	Number coherentGain(const std::vector<Number>& window) {
		if (window.empty()) {
			throw std::invalid_argument("coherentGain: the window is empty");
		}
		Number total = Number(0);
		for (const Number& value : window) {
			total += value;
		}
		return total / static_cast<Number>(window.size());
	}

	/// @brief  The equivalent noise bandwidth, in bins.
	/// @param  window  Window values.
	/// @return N * sum(w^2) / (sum(w))^2: the width of the filter a single bin
	///         represents, which is how much broadband noise the window admits.
	/// @throws std::invalid_argument  When the window is empty or sums to zero.
	/// @brief  The compact window catalogue used by the resampling kernels and
	///         by callers that only need one window shape.
	///
	/// WindowType remains the extended analysis catalogue (Bartlett, Bohman,
	/// Gaussian, Lanczos, ...); WindowId names the nine shapes that the
	/// resampling and kernel code shares.
	enum class WindowId {
		Rectangular,		///< All ones.
		Hann,				///< Raised cosine.
		Hamming,			///< Raised cosine with a pedestal.
		Blackman,			///< Three-term cosine.
		BlackmanHarris,		///< Four-term cosine.
		Nuttall,			///< Three-term, continuous derivative.
		FlatTop,			///< Wide, amplitude-accurate main lobe.
		Kaiser,				///< Bessel-shaped; param is beta.
		Tukey				///< Rectangular core with cosine tapers; param is alpha.
	};

	/// @brief  Evaluates a window at one continuous normalised position.
	/// @param  id        Window shape.
	/// @param  position  Position in [0, 1]; values outside are clamped.
	/// @param  param     Kaiser beta or Tukey alpha; ignored by the others.
	/// @return The window value at that position.
	/// @note   The convention is symmetric: position 0 and 1 are the endpoints.
	inline double windowPoint(const WindowId id, const double position, const double param = 0.5) {
		const double u = std::min(1.0, std::max(0.0, position));
		const double angle = 6.283185307179586476925286766559 * u;
		switch (id) {
		case WindowId::Rectangular:
			return 1.0;

		case WindowId::Hann:
			return 0.5 - 0.5 * std::cos(angle);

		case WindowId::Hamming:
			return 0.54 - 0.46 * std::cos(angle);

		case WindowId::Blackman:
			return 0.42 - 0.5 * std::cos(angle) + 0.08 * std::cos(2.0 * angle);

		case WindowId::BlackmanHarris:
			return 0.35875 - 0.48829 * std::cos(angle) + 0.14128 * std::cos(2.0 * angle)
				- 0.01168 * std::cos(3.0 * angle);

		case WindowId::Nuttall:
			return 0.375 - 0.5 * std::cos(angle) + 0.125 * std::cos(2.0 * angle);

		case WindowId::FlatTop:
			return 0.21557895 - 0.41663158 * std::cos(angle) + 0.277263158 * std::cos(2.0 * angle)
				- 0.083578947 * std::cos(3.0 * angle) + 0.006947368 * std::cos(4.0 * angle);

		case WindowId::Kaiser: {
			const double centred = 2.0 * u - 1.0;
			const double radical = std::sqrt(std::max(0.0, 1.0 - centred * centred));
			const double denominator = besselI0<double>(param);
			return (denominator == 0.0) ? 1.0 : besselI0<double>(param * radical) / denominator;
		}

		case WindowId::Tukey: {
			const double alpha = std::min(1.0, std::max(0.0, param));
			if (alpha <= 0.0) {
				return 1.0;
			}
			if (alpha >= 1.0) {
				return 0.5 - 0.5 * std::cos(angle);
			}
			if (u < alpha / 2.0) {
				return 0.5 * (1.0 + std::cos(3.14159265358979323846
					* (2.0 * u / alpha - 1.0)));
			}
			if (u > 1.0 - alpha / 2.0) {
				return 0.5 * (1.0 + std::cos(3.14159265358979323846
					* (2.0 * u / alpha - 2.0 / alpha + 1.0)));
			}
			return 1.0;
		}
		}
		return 1.0;
	}

	/// @brief  One sample of a window of n points.
	/// @param  id     Window shape.
	/// @param  index  Sample index, 0 based.
	/// @param  n      Number of points; zero returns zero.
	/// @param  param  Kaiser beta or Tukey alpha.
	/// @return The symmetric window value at index.
	inline double windowValue(const WindowId id, const std::size_t index, const std::size_t n,
		const double param = 0.5) {
		if (n == 0 || index >= n) {
			return 0.0;
		}
		if (n == 1) {
			return 1.0;
		}
		return windowPoint(id, static_cast<double>(index) / static_cast<double>(n - 1), param);
	}

	/// @brief  Builds a window of n points.
	/// @param  id     Window shape.
	/// @param  n      Number of points.
	/// @param  param  Kaiser beta or Tukey alpha.
	/// @return The window samples, symmetric with endpoints at position 0 and 1.
	inline std::vector<double> window(const WindowId id, const std::size_t n,
		const double param = 0.5) {
		std::vector<double> values(n, 0.0);
		for (std::size_t index = 0; index < n; ++index) {
			values[index] = windowValue(id, index, n, param);
		}
		return values;
	}

	/// @brief  The coherent gain of a named window.
	/// @param  id     Window shape.
	/// @param  n      Number of points.
	/// @param  param  Kaiser beta or Tukey alpha.
	/// @return The mean window value.
	inline double coherentGain(const WindowId id, const std::size_t n, const double param = 0.5) {
		return coherentGain(window(id, n, param));
	}
}
