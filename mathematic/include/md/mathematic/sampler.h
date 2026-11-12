/// @file   sampler.h
/// @brief  The transform engine: discrete and mixed-radix Fourier transforms,
///         Bluestein's algorithm for arbitrary lengths, the analytic signal,
///         the discrete Mellin transform, the Morlet continuous wavelet
///         transform, and the interpolation / resampling entry points that
///         rebuild a signal on a new time base.
///
/// The continuous Laplace and Mellin transforms live in transforms/laplace.h
/// and transforms/mellin.h; this header deliberately does not include them.

#pragma once

#include <md/mathematic/frequency.h>
#include <md/mathematic/kernel.h>
#include <md/mathematic/numerical.h>
#include <md/mathematic/sample.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace md::mathematic {

	/// @brief  A precomputed plan for transforming sequences of one fixed length.
	///
	/// Building a plan factors the length into radices 2, 3, 5 and 7 and prepares
	/// the chirp used by Bluestein's algorithm when the length has a larger prime
	/// factor. The plan is immutable, so one instance may be shared by any number
	/// of threads.
	///
	/// Conventions, chosen to agree with numpy:
	///  - forward() is unnormalised;
	///  - inverse() multiplies by 1/n;
	///  - transformReal() returns the n/2+1 non-negative frequency bins.
	///
	/// @tparam Number  Floating-point type used for the samples.
	template<typename Number = double>
	class FourierPlan {
	private:
		std::size_t length = 0;						///< Number of samples in one transform.
		std::vector<std::size_t> radices;			///< Mixed-radix decomposition of the length.
		bool direct = false;						///< True when the length is small enough for a direct sum.
		bool useBluestein = false;					///< True when a prime factor above seven remains.
		std::size_t innerLength = 0;				///< Power-of-two convolution length for Bluestein.
		std::vector<Complex<Number>> roots;			///< W_n^j for j in [0, n), the twiddle table.
		std::vector<Complex<Number>> innerRoots;	///< Twiddle table of the Bluestein inner length.
		std::vector<Complex<Number>> chirp;			///< e^{-i pi k^2 / n}, k in [0, n).
		std::vector<Complex<Number>> kernel;		///< Transform of the Bluestein chirp kernel.

		/// @brief  Fills a twiddle table with W_size^j.
		/// @param  table  Destination, resized to @p size.
		/// @param  size   Transform length the table belongs to.
		static void buildRoots(std::vector<Complex<Number>>& table, const std::size_t size) {
			table.resize(size);
			for (std::size_t index = 0; index < size; ++index) {
				const Number angle = -DoublePi<Number> * static_cast<Number>(index)
					/ static_cast<Number>(size);
				table[index] = Complex<Number>(static_cast<Number>(std::cos(angle)),
					static_cast<Number>(std::sin(angle)));
			}
		}

		/// @brief  Direct O(n^2) forward transform, used for short and base-case blocks.
		/// @param  data       Values to transform in place.
		/// @param  table      Twiddle table of the enclosing transform.
		/// @param  tableSize  Size of @p table.
		/// @param  stride     Table step that yields the first root of this block.
		static void directTransform(std::vector<Complex<Number>>& data, const Complex<Number>* table,
			const std::size_t tableSize, const std::size_t stride) {
			const std::size_t n = data.size();
			if (n == 0) {
				return;
			}
			std::vector<Complex<Number>> result(n);
			for (std::size_t k = 0; k < n; ++k) {
				const std::size_t step = (k * stride) % tableSize;
				std::size_t index = 0;
				Complex<Number> sum;
				for (std::size_t j = 0; j < n; ++j) {
					sum += data[j] * table[index];
					index += step;
					if (index >= tableSize) {
						index -= tableSize;
					}
				}
				result[k] = sum;
			}
			data.swap(result);
		}

		/// @brief  Recursive mixed-radix decimation-in-time transform.
		/// @param  data       Values to transform; replaced by the transform.
		/// @param  factors    Radices still to apply, in order.
		/// @param  factorCount  Number of remaining factors.
		/// @param  table      Twiddle table of the enclosing transform.
		/// @param  tableSize  Size of @p table.
		/// @param  stride     Table step that yields the first root of this block.
		static void mixedRadixTransform(std::vector<Complex<Number>>& data, const std::size_t* factors,
			const std::size_t factorCount, const Complex<Number>* table, const std::size_t tableSize,
			const std::size_t stride) {
			const std::size_t n = data.size();
			if (n <= 1) {
				return;
			}
			if (factorCount == 0 || n <= 16) {
				directTransform(data, table, tableSize, stride);
				return;
			}

			const std::size_t radix = factors[0];
			const std::size_t block = n / radix;
			if (block <= 1) {
				directTransform(data, table, tableSize, stride);
				return;
			}

			// y_t[u] = x[u * radix + t]: one length-block sequence per phase.
			std::vector<std::vector<Complex<Number>>> subsequences(radix);
			for (std::size_t t = 0; t < radix; ++t) {
				subsequences[t].resize(block);
				for (std::size_t u = 0; u < block; ++u) {
					subsequences[t][u] = data[u * radix + t];
				}
			}

			for (std::size_t t = 0; t < radix; ++t) {
				mixedRadixTransform(subsequences[t], factors + 1, factorCount - 1, table, tableSize,
					stride * radix);
			}

			// X[k] = sum over t of W_n^(t k) * Y_t[k mod block].
			std::vector<Complex<Number>> result(n);
			for (std::size_t k = 0; k < n; ++k) {
				const std::size_t reduced = k % block;
				const std::size_t step = (k * stride) % tableSize;
				std::size_t index = 0;
				Complex<Number> sum;
				for (std::size_t t = 0; t < radix; ++t) {
					sum += subsequences[t][reduced] * table[index];
					index += step;
					if (index >= tableSize) {
						index -= tableSize;
					}
				}
				result[k] = sum;
			}
			data.swap(result);
		}

		/// @brief  Runs the forward transform described by a radix list.
		static void run(std::vector<Complex<Number>>& data, const std::vector<std::size_t>& factors,
			const std::vector<Complex<Number>>& table) {
			if (data.empty()) {
				return;
			}
			mixedRadixTransform(data, factors.data(), factors.size(), table.data(), table.size(), 1);
		}

		/// @brief  Bluestein forward transform, using the prepared chirp.
		void bluesteinForward(std::vector<Complex<Number>>& data) const {
			std::vector<Complex<Number>> buffer(innerLength);
			for (std::size_t index = 0; index < length; ++index) {
				buffer[index] = data[index] * chirp[index];
			}
			std::vector<std::size_t> innerRadices;
			for (std::size_t value = innerLength; value > 1; value /= 2) {
				innerRadices.push_back(2);
			}
			run(buffer, innerRadices, innerRoots);
			for (std::size_t index = 0; index < innerLength; ++index) {
				buffer[index] *= kernel[index];
			}
			// Inverse by conjugation, so the forward transform stays the only kernel.
			for (Complex<Number>& value : buffer) {
				value = value.conjugate();
			}
			run(buffer, innerRadices, innerRoots);
			const Number scale = One<Number> / static_cast<Number>(innerLength);
			for (std::size_t index = 0; index < length; ++index) {
				data[index] = buffer[index].conjugate() * scale * chirp[index];
			}
		}

		/// @brief  Returns the smallest power of two not below a value.
		static std::size_t nextPowerOfTwo(std::size_t value) {
			std::size_t result = 1;
			while (result < value) {
				result *= 2;
			}
			return result;
		}

		/// @brief  Builds the Bluestein chirp and kernel for the stored length.
		void prepareBluestein() {
			chirp.resize(length);
			for (std::size_t index = 0; index < length; ++index) {
				const std::size_t square = (index * index) % (2 * length);
				const Number angle = -Pi<Number> * static_cast<Number>(square) / static_cast<Number>(length);
				chirp[index] = Complex<Number>(static_cast<Number>(std::cos(angle)),
					static_cast<Number>(std::sin(angle)));
			}

			innerLength = nextPowerOfTwo(2 * length - 1);
			buildRoots(innerRoots, innerLength);
			std::vector<Complex<Number>> buffer(innerLength);
			buffer[0] = chirp[0].conjugate();
			for (std::size_t index = 1; index < length; ++index) {
				const Complex<Number> value = chirp[index].conjugate();
				buffer[index] = value;
				buffer[innerLength - index] = value;
			}

			std::vector<std::size_t> innerRadices;
			for (std::size_t value = innerLength; value > 1; value /= 2) {
				innerRadices.push_back(2);
			}
			run(buffer, innerRadices, innerRoots);
			kernel = std::move(buffer);
		}

	public:
		/// @brief  Constructs an empty plan, which transforms nothing.
		FourierPlan() = default;

		/// @brief  Builds a plan for a sequence length.
		/// @param  size  Number of samples per transform; zero is allowed and
		///               yields an empty plan.
		/// @return The plan.
		static FourierPlan forSize(const std::size_t size) {
			FourierPlan plan;
			plan.length = size;
			if (size == 0) {
				return plan;
			}
			buildRoots(plan.roots, size);
			if (size <= 1) {
				return plan;
			}
			if (size <= 16) {
				plan.direct = true;
				return plan;
			}

			std::size_t remaining = size;
			for (const std::size_t radix : { std::size_t{ 2 }, std::size_t{ 3 }, std::size_t{ 5 },
				std::size_t{ 7 } }) {
				while (remaining % radix == 0) {
					plan.radices.push_back(radix);
					remaining /= radix;
				}
			}

			if (remaining != 1) {
				plan.useBluestein = true;
				plan.prepareBluestein();
			}
			return plan;
		}

		/// @brief  Number of samples in one transform.
		std::size_t size() const {
			return length;
		}

		/// @brief  Reports whether the plan transforms anything.
		bool empty() const {
			return length == 0;
		}

		/// @brief  Reports whether a prime factor above seven forced Bluestein.
		bool isBluestein() const {
			return useBluestein;
		}

		/// @brief  Forward transform, in place and unnormalised.
		/// @param  data  Values to transform; its size must equal size().
		/// @throws std::invalid_argument  When the size does not match the plan.
		void forward(std::vector<Complex<Number>>& data) const {
			if (data.size() != length) {
				throw std::invalid_argument("FourierPlan::forward: the sequence length does not match the plan");
			}
			if (length <= 1) {
				return;
			}
			if (direct) {
				directTransform(data, roots.data(), roots.size(), 1);
			}
			else if (useBluestein) {
				bluesteinForward(data);
			}
			else {
				run(data, radices, roots);
			}
		}

		/// @brief  Inverse transform, in place, multiplied by 1/n.
		/// @param  data  Values to transform; its size must equal size().
		/// @throws std::invalid_argument  When the size does not match the plan.
		void inverse(std::vector<Complex<Number>>& data) const {
			if (data.size() != length) {
				throw std::invalid_argument("FourierPlan::inverse: the sequence length does not match the plan");
			}
			if (length <= 1) {
				return;
			}
			for (Complex<Number>& value : data) {
				value = value.conjugate();
			}
			forward(data);
			const Number scale = One<Number> / static_cast<Number>(length);
			for (Complex<Number>& value : data) {
				value = value.conjugate() * scale;
			}
		}

		/// @brief  Returns the forward transform of a sequence.
		/// @param  data  Values to transform.
		/// @return A new sequence holding the transform.
		std::vector<Complex<Number>> transform(const std::vector<Complex<Number>>& data) const {
			std::vector<Complex<Number>> result(data);
			forward(result);
			return result;
		}

		/// @brief  Returns the non-negative frequency bins of a real sequence.
		/// @param  data  Real values to transform.
		/// @return n/2+1 complex bins, the same values numpy reports for rfft.
		std::vector<Complex<Number>> transformReal(const std::vector<Number>& data) const {
			if (length == 0) {
				return {};
			}
			std::vector<Complex<Number>> buffer(data.size());
			for (std::size_t index = 0; index < data.size(); ++index) {
				buffer[index] = Complex<Number>(data[index], Zero<Number>);
			}
			forward(buffer);
			buffer.resize(length / 2 + 1);
			return buffer;
		}

		/// @brief  The frequency of each bin of a real transform.
		/// @param  sampleRate  Sampling rate of the original signal, in hertz.
		/// @return k * sampleRate / n for k in [0, n/2].
		std::vector<Number> binFrequencies(const Number& sampleRate) const {
			if (length == 0) {
				return {};
			}
			std::vector<Number> result(length / 2 + 1);
			for (std::size_t index = 0; index < result.size(); ++index) {
				result[index] = static_cast<Number>(index) * sampleRate / static_cast<Number>(length);
			}
			return result;
		}
	};

	/// @brief  The geometric sampling grid used by the discrete Mellin transform.
	///
	/// Samples are taken at t_k = startTime * ratio^k. A ratio above one places
	/// the samples logarithmically, which turns a scale change into a shift and
	/// makes the transform computable by a plain Fourier transform.
	///
	/// @tparam Number  Floating-point type used for the grid.
	template<typename Number = double>
	class MellinGrid {
	public:
		Number startTime = One<Number>;				///< Time of the first sample, in seconds.
		Number ratio = static_cast<Number>(1.1);	///< Spacing ratio between neighbouring samples.
		std::size_t count = 0;						///< Number of samples on the grid.

		/// @brief  The uniform step on the logarithmic time axis.
		Number logStep() const {
			return std::log(ratio);
		}

		/// @brief  The time of one grid sample.
		/// @param  index  Zero-based grid position.
		Number timeAt(const std::size_t index) const {
			return startTime * static_cast<Number>(std::pow(ratio, static_cast<Number>(index)));
		}
	};

	/// @brief  The transform engine, together with the sampling metadata the
	///         transforms need.
	///
	/// Every transform is a static function taking and returning plain vectors,
	/// so the type carries no hidden state beyond the rate and the sample count
	/// of the buffer it describes.
	///
	/// @tparam Number  Floating-point type used for the samples.
	template<typename Number = double>
	class Sampler {
	private:
		Sample<Number> sampleValue;	///< Metadata of the buffer being transformed.

	public:
		/// @brief  Constructs a sampler over an empty buffer.
		Sampler() = default;

		/// @brief  Constructs a sampler over a described buffer.
		/// @param  sample  Buffer metadata.
		explicit Sampler(const Sample<Number>& sample) : sampleValue(sample) {}

		/// @brief  The buffer metadata.
		const Sample<Number>& sample() const {
			return sampleValue;
		}

		/// @brief  Replaces the buffer metadata.
		void setSample(const Sample<Number>& sample) {
			sampleValue = sample;
		}

		/// @brief  The frequency of one bin for this sampler's rate.
		/// @param  index  Zero-based bin index.
		/// @param  size   Transform length.
		Number binFrequency(const std::size_t index, const std::size_t size) const {
			if (size == 0) {
				return Zero<Number>;
			}
			return static_cast<Number>(index) * sampleValue.rate().hertz() / static_cast<Number>(size);
		}

		/// @brief  Direct O(n^2) Fourier transform, used as the reference implementation.
		/// @param  data  Values to transform.
		/// @return The unnormalised forward transform.
		static std::vector<Complex<Number>> discreteFourierTransform(
			const std::vector<Complex<Number>>& data) {
			std::vector<Complex<Number>> result(data);
			if (result.size() <= 1) {
				return result;
			}
			const std::size_t n = result.size();
			std::vector<Complex<Number>> transformed(n);
			for (std::size_t k = 0; k < n; ++k) {
				Complex<Number> sum;
				for (std::size_t j = 0; j < n; ++j) {
					const std::size_t product = (j * k) % n;
					const Number angle = -DoublePi<Number> * static_cast<Number>(product)
						/ static_cast<Number>(n);
					sum += data[j] * Complex<Number>(static_cast<Number>(std::cos(angle)),
						static_cast<Number>(std::sin(angle)));
				}
				transformed[k] = sum;
			}
			return transformed;
		}

		/// @brief  Mixed-radix Fourier transform for any length.
		/// @param  data  Values to transform.
		/// @return The unnormalised forward transform; prime lengths use Bluestein.
		static std::vector<Complex<Number>> fastFourierTransform(const std::vector<Complex<Number>>& data) {
			return FourierPlan<Number>::forSize(data.size()).transform(data);
		}

		/// @brief  Radix-two transform, for lengths that are powers of two.
		/// @param  data  Values to transform in place.
		/// @throws std::invalid_argument  When the length is not a power of two.
		static void radixTwo(std::vector<Complex<Number>>& data) {
			applyRadix(data, 2);
		}

		/// @brief  Radix-three transform, for lengths that are powers of three.
		/// @param  data  Values to transform in place.
		/// @throws std::invalid_argument  When the length is not a power of three.
		static void radixThree(std::vector<Complex<Number>>& data) {
			applyRadix(data, 3);
		}

		/// @brief  Radix-five transform, for lengths that are powers of five.
		/// @param  data  Values to transform in place.
		/// @throws std::invalid_argument  When the length is not a power of five.
		static void radixFive(std::vector<Complex<Number>>& data) {
			applyRadix(data, 5);
		}

		/// @brief  Radix-seven transform, for lengths that are powers of seven.
		/// @param  data  Values to transform in place.
		/// @throws std::invalid_argument  When the length is not a power of seven.
		static void radixSeven(std::vector<Complex<Number>>& data) {
			applyRadix(data, 7);
		}

		/// @brief  Bluestein transform, valid for every length.
		/// @param  data  Values to transform.
		/// @return The unnormalised forward transform.
		static std::vector<Complex<Number>> bluestein(const std::vector<Complex<Number>>& data) {
			return FourierPlan<Number>::forSize(data.size()).transform(data);
		}

		/// @brief  The analytic signal of a real sequence.
		/// @param  data  Real values.
		/// @return x + i H[x], the complex signal whose argument is the
		///         instantaneous phase and whose modulus is the envelope.
		/// @note   The negative frequencies are zeroed and the positive ones are
		///         doubled, which is the discrete Hilbert transform computed with
		///         one forward and one inverse transform.
		static std::vector<Complex<Number>> analyticSignal(const std::vector<Number>& data) {
			const std::size_t n = data.size();
			if (n == 0) {
				return {};
			}
			std::vector<Complex<Number>> buffer(n);
			for (std::size_t index = 0; index < n; ++index) {
				buffer[index] = Complex<Number>(data[index], Zero<Number>);
			}

			FourierPlan<Number> plan = FourierPlan<Number>::forSize(n);
			plan.forward(buffer);

			const std::size_t positiveEnd = (n % 2 == 0) ? n / 2 : (n + 1) / 2;
			for (std::size_t index = 1; index < positiveEnd; ++index) {
				buffer[index] *= static_cast<Number>(2);
			}
			for (std::size_t index = positiveEnd + ((n % 2 == 0) ? 1 : 0); index < n; ++index) {
				buffer[index] = Complex<Number>();
			}

			plan.inverse(buffer);
			return buffer;
		}

		/// @brief  The discrete Mellin transform of a geometrically sampled signal.
		/// @param  samples  Values sampled at grid.timeAt(0), grid.timeAt(1), ...
		/// @param  grid     The geometric grid the samples lie on.
		/// @param  sigma    Real part of the transform variable, zero for the
		///                  plain scale spectrum.
		/// @return M(sigma + i omega_m) = logStep * sum over k of f(t_k) t_k^sigma
		///         exp(-i 2 pi m k / K), with omega_m = 2 pi m / (K logStep).
		static std::vector<Complex<Number>> mellinTransform(const std::vector<Number>& samples,
			const MellinGrid<Number>& grid, const Number& sigma = Zero<Number>) {
			const std::size_t count = std::min(samples.size(), grid.count);
			if (count == 0) {
				return {};
			}
			if (!(grid.ratio > Zero<Number>) || !(grid.startTime > Zero<Number>)) {
				throw std::invalid_argument("Sampler::mellinTransform: the grid must start at a positive time with a ratio above one");
			}

			std::vector<Complex<Number>> buffer(count);
			for (std::size_t index = 0; index < count; ++index) {
				const Number time = grid.timeAt(index);
				const Number weight = static_cast<Number>(std::pow(time, sigma));
				buffer[index] = Complex<Number>(samples[index] * weight, Zero<Number>);
			}

			FourierPlan<Number>::forSize(count).forward(buffer);
			const Number scale = grid.logStep();
			for (Complex<Number>& value : buffer) {
				value *= scale;
			}
			return buffer;
		}

		/// @brief  The Morlet continuous wavelet transform of a real signal.
		/// @param  signal          Real samples, uniformly spaced.
		/// @param  sampleInterval  Spacing between neighbouring samples, in seconds.
		/// @param  scales          Scale factors, in seconds; each must be positive.
		/// @param  centerFrequency Morlet centre frequency in radians, six by default.
		/// @return One row per scale and one column per sample, holding
		///         CWT(s, tau) = 1/sqrt(s) * sum of x(t) psi*((t - tau)/s) dt.
		static std::vector<std::vector<Complex<Number>>> waveletTransform(
			const std::vector<Number>& signal, const Number& sampleInterval,
			const std::vector<Number>& scales, const Number& centerFrequency = static_cast<Number>(6)) {
			if (!(sampleInterval > Zero<Number>)) {
				throw std::invalid_argument("Sampler::waveletTransform: the sample interval must be positive");
			}
			std::vector<std::vector<Complex<Number>>> result;
			if (signal.empty() || scales.empty()) {
				return result;
			}

			const std::size_t count = signal.size();
			const Number normalization = static_cast<Number>(std::pow(Pi<Number>, -0.25));
			result.resize(scales.size());

			for (std::size_t scaleIndex = 0; scaleIndex < scales.size(); ++scaleIndex) {
				const Number scale = scales[scaleIndex];
				if (!(scale > Zero<Number>)) {
					throw std::invalid_argument("Sampler::waveletTransform: every scale must be positive");
				}

				// The Gaussian support is truncated at four standard deviations.
				const std::size_t reach = static_cast<std::size_t>(
					std::ceil(static_cast<double>(4 * scale / sampleInterval)));
				const Number envelopeScale = normalization / static_cast<Number>(std::sqrt(scale));

				result[scaleIndex].resize(count);
				for (std::size_t tau = 0; tau < count; ++tau) {
					const std::size_t first = tau > reach ? tau - reach : 0;
					const std::size_t last = std::min(count - 1, tau + reach);
					Complex<Number> sum;
					for (std::size_t index = first; index <= last; ++index) {
						const Number offset = (static_cast<Number>(index) - static_cast<Number>(tau))
							* sampleInterval / scale;
						const Number gaussian = static_cast<Number>(std::exp(-0.5 * offset * offset));
						const Number angle = centerFrequency * offset;
						const Complex<Number> wavelet(gaussian * static_cast<Number>(std::cos(angle)),
							-gaussian * static_cast<Number>(std::sin(angle)));
						sum += wavelet * signal[index];
					}
					result[scaleIndex][tau] = sum * envelopeScale * sampleInterval;
				}
			}
			return result;
		}

		/// @brief  Rebuilds a signal at one arbitrary time.
		/// @param  x       Uniform sample positions, strictly increasing.
		/// @param  y       Sample values, one per position.
		/// @param  query   Time to evaluate.
		/// @param  kernel  Reconstruction kernel.
		/// @return The interpolated value, or zero outside the sampled range.
		/// @throws std::invalid_argument  When the arrays differ in size, the
		///         positions are not uniform, or fewer than two samples exist.
		/// @note   A query that lands exactly on a sample returns that sample,
		///         including for the non-interpolating BSpline kernel.
		static Number sampleAt(const std::vector<Number>& x, const std::vector<Number>& y,
			const Number& query, const KernelId kernel = KernelId::Lanczos3) {
			requireSamples(x, y, "Sampler::sampleAt");
			if (x.size() < 2) {
				throw std::invalid_argument("Sampler::sampleAt: at least two samples are required");
			}
			const Number step = uniformStep(x, "Sampler::sampleAt");
			return sampleUniform(x, y, step, query, kernel);
		}

		/// @brief  Rebuilds a signal on a new set of sample positions.
		/// @param  x       Uniform input positions, strictly increasing.
		/// @param  y       Input values, one per position.
		/// @param  xNew    Output positions.
		/// @param  kernel  Reconstruction kernel.
		/// @return One value per output position; zero outside the input range.
		static std::vector<Number> resample(const std::vector<Number>& x, const std::vector<Number>& y,
			const std::vector<Number>& xNew, const KernelId kernel = KernelId::Lanczos3) {
			requireSamples(x, y, "Sampler::resample");
			if (x.size() < 2) {
				throw std::invalid_argument("Sampler::resample: at least two samples are required");
			}
			const Number step = uniformStep(x, "Sampler::resample");
			std::vector<Number> result;
			result.reserve(xNew.size());
			for (const Number& query : xNew) {
				result.push_back(sampleUniform(x, y, step, query, kernel));
			}
			return result;
		}

		/// @brief  Resamples a uniformly sampled signal by the rational factor
		///         numerator / denominator, with polyphase decomposition.
		/// @param  signal       Input samples, uniformly spaced.
		/// @param  numerator    Upsampling factor L.
		/// @param  denominator  Downsampling factor M.
		/// @param  kernel       Reconstruction kernel.
		/// @return ceil(size * L / M) output samples.
		/// @throws std::invalid_argument  When a factor is zero.
		/// @note   When L/M < 1, a windowed-sinc low-pass is applied before the
		///         phase bank is built. Its half-length is ceil(4 / (L/M)), so
		///         the filter has 8 * M/L + 1 taps: the WindowedSinc support
		///         scaled by the cutoff. The polyphase bank evaluates only the
		///         phases the output actually uses.
		static std::vector<Number> resampleRatio(const std::vector<Number>& signal,
			std::size_t numerator, std::size_t denominator, const KernelId kernel = KernelId::Lanczos3) {
			if (numerator == 0 || denominator == 0) {
				throw std::invalid_argument("Sampler::resampleRatio: the factors must be positive");
			}
			if (signal.empty()) {
				return {};
			}

			const std::size_t divisor = std::gcd(numerator, denominator);
			const std::size_t up = numerator / divisor;
			const std::size_t down = denominator / divisor;

			std::vector<Number> source = signal;
			if (up < down) {
				const Number cutoff = static_cast<Number>(up) / static_cast<Number>(down);
				const std::size_t halfLength = std::max<std::size_t>(4,
					static_cast<std::size_t>(std::ceil(4.0 / static_cast<double>(cutoff))));
				source = lowPass(signal, cutoff, halfLength);
			}

			const std::size_t count = source.size();
			const std::size_t outputLength = (count * up + down - 1) / down;
			std::vector<Number> result(outputLength, Number(0));

			const long long reach = static_cast<long long>(std::ceil(kernelSupport(kernel)));
			for (std::size_t phase = 0; phase < up; ++phase) {
				const std::size_t position = phase * down;
				const std::size_t whole = position / up;
				const Number fraction = static_cast<Number>(position % up) / static_cast<Number>(up);

				for (std::size_t index = phase; index < outputLength; index += up) {
					const std::size_t block = (index - phase) / up;
					const long long base = static_cast<long long>(block * down + whole);
					Number total = Number(0);
					Number weightTotal = Number(0);
					for (long long tap = -reach; tap <= reach; ++tap) {
						const long long sourceIndex = base + tap;
						if (sourceIndex < 0 || sourceIndex >= static_cast<long long>(count)) {
							continue;
						}
						const Number weight = kernelValue(kernel, fraction - static_cast<Number>(tap));
						total += weight * source[static_cast<std::size_t>(sourceIndex)];
						weightTotal += weight;
					}
					result[index] = (std::abs(weightTotal) > std::numeric_limits<Number>::epsilon())
						? total / weightTotal : Number(0);
				}
			}
			return result;
		}

		/// @brief  Resamples a uniformly sampled signal by L / M.
		/// @param  x            Uniform input positions, strictly increasing.
		/// @param  y            Input values, one per position.
		/// @param  numerator    Upsampling factor L.
		/// @param  denominator  Downsampling factor M.
		/// @param  kernel       Reconstruction kernel.
		/// @return The resampled values; the first output sits at x.front().
		static std::vector<Number> resampleRatio(const std::vector<Number>& x, const std::vector<Number>& y,
			std::size_t numerator, std::size_t denominator, const KernelId kernel = KernelId::Lanczos3) {
			requireSamples(x, y, "Sampler::resampleRatio");
			if (x.size() < 2) {
				throw std::invalid_argument("Sampler::resampleRatio: at least two samples are required");
			}
			uniformStep(x, "Sampler::resampleRatio");
			return resampleRatio(y, numerator, denominator, kernel);
		}

		/// @brief  Delays a signal by a fractional number of samples.
		/// @param  signal   Input samples, uniformly spaced.
		/// @param  delay    Delay in samples; positive delays the signal.
		/// @param  kernel   Reconstruction kernel.
		/// @param  lagrange When true, cubic kernels use four-point Lagrange
		///                  weights (the Farrow form of the cubic case).
		/// @return A signal of the same length; samples shifted in from outside
		///         the input are zero.
		static std::vector<Number> fractionalDelay(const std::vector<Number>& signal,
			const Number& delay, const KernelId kernel = KernelId::CatmullRom,
			const bool lagrange = false) {
			std::vector<Number> result(signal.size(), Number(0));
			const long long count = static_cast<long long>(signal.size());
			const Number support = kernelSupport(kernel);

			for (long long index = 0; index < count; ++index) {
				const Number position = static_cast<Number>(index) - delay;
				const long long nearest = static_cast<long long>(std::llround(position));
				if (nearest >= 0 && nearest < count
					&& std::abs(position - static_cast<Number>(nearest)) <= static_cast<Number>(1.0e-12)) {
					result[static_cast<std::size_t>(index)] = signal[static_cast<std::size_t>(nearest)];
					continue;
				}

				if (lagrange) {
					const long long base = static_cast<long long>(std::floor(position));
					const Number fraction = position - static_cast<Number>(base);
					const Number weight0 = -fraction * (fraction - Number(1)) * (fraction - Number(2)) / Number(6);
					const Number weight1 = (fraction + Number(1)) * (fraction - Number(1))
						* (fraction - Number(2)) / Number(2);
					const Number weight2 = -(fraction + Number(1)) * fraction
						* (fraction - Number(2)) / Number(2);
					const Number weight3 = (fraction + Number(1)) * fraction
						* (fraction - Number(1)) / Number(6);
					const Number weights[4] = { weight0, weight1, weight2, weight3 };
					Number total = Number(0);
					for (long long tap = 0; tap < 4; ++tap) {
						const long long sourceIndex = base - 1 + tap;
						if (sourceIndex >= 0 && sourceIndex < count) {
							total += weights[tap] * signal[static_cast<std::size_t>(sourceIndex)];
						}
					}
					result[static_cast<std::size_t>(index)] = total;
					continue;
				}

				const long long first = static_cast<long long>(std::ceil(position - support));
				const long long last = static_cast<long long>(std::floor(position + support));
				Number total = Number(0);
				Number weightTotal = Number(0);
				for (long long sourceIndex = first; sourceIndex <= last; ++sourceIndex) {
					if (sourceIndex < 0 || sourceIndex >= count) {
						continue;
					}
					const Number weight = kernelValue(kernel, position - static_cast<Number>(sourceIndex));
					total += weight * signal[static_cast<std::size_t>(sourceIndex)];
					weightTotal += weight;
				}
				result[static_cast<std::size_t>(index)] =
					(std::abs(weightTotal) > std::numeric_limits<Number>::epsilon())
					? total / weightTotal : Number(0);
			}
			return result;
		}

		/// @brief  Low-passes and downsamples by an integer factor.
		/// @param  signal  Input samples.
		/// @param  factor  Decimation factor, at least one.
		/// @param  kernel  Kernel used to build the anti-alias filter.
		/// @return One sample out of every factor, after a zero-phase low-pass
		///         whose half-length is ceil(4 * factor).
		static std::vector<Number> decimate(const std::vector<Number>& signal, const std::size_t factor,
			const KernelId kernel = KernelId::WindowedSinc) {
			if (factor == 0) {
				throw std::invalid_argument("Sampler::decimate: the factor must be positive");
			}
			if (factor == 1 || signal.empty()) {
				return signal;
			}
			const std::size_t halfLength = std::max<std::size_t>(4,
				static_cast<std::size_t>(std::ceil(4.0 * static_cast<double>(factor))));
			const std::vector<Number> filtered = lowPass(signal, Number(1) / static_cast<Number>(factor),
				halfLength, kernel);
			const std::size_t outputLength = (signal.size() + factor - 1) / factor;
			std::vector<Number> result(outputLength, Number(0));
			for (std::size_t index = 0; index < outputLength; ++index) {
				result[index] = filtered[index * factor];
			}
			return result;
		}

		/// @brief  Upsamples by an integer factor with a zero-phase low-pass.
		/// @param  signal  Input samples.
		/// @param  factor  Interpolation factor, at least one.
		/// @param  kernel  Kernel used to build the interpolation filter.
		/// @return (size - 1) * factor + 1 samples, so sample n * factor is the
		///         delayed-free image of input sample n.
		static std::vector<Number> interpolate(const std::vector<Number>& signal, const std::size_t factor,
			const KernelId kernel = KernelId::WindowedSinc) {
			if (factor == 0) {
				throw std::invalid_argument("Sampler::interpolate: the factor must be positive");
			}
			if (factor == 1 || signal.empty()) {
				return signal;
			}
			const std::size_t outputLength = (signal.size() - 1) * factor + 1;
			std::vector<Number> zeros(outputLength, Number(0));
			for (std::size_t index = 0; index < signal.size(); ++index) {
				zeros[index * factor] = signal[index];
			}
			const std::size_t halfLength = std::max<std::size_t>(4,
				static_cast<std::size_t>(std::ceil(4.0 * static_cast<double>(factor))));
			std::vector<Number> weights = lowPassWeights(Number(1) / static_cast<Number>(factor),
				halfLength, kernel);
			for (Number& weight : weights) {
				weight *= static_cast<Number>(factor);
			}
			std::vector<Number> result = filterCentered(zeros, weights);
			// Preserve the original samples exactly; a finite windowed-sinc
			// filter has a small passband ripple, and pinning the known values
			// is both conventional and what makes decimate/interpolate align.
			for (std::size_t index = 0; index < signal.size(); ++index) {
				result[index * factor] = signal[index];
			}
			return result;
		}

		/// @brief  Samples a signal along an arbitrary monotone time map.
		/// @param  x       Uniform input positions, strictly increasing.
		/// @param  y       Input values, one per position.
		/// @param  map     Maps an input position to the position to read.
		/// @param  kernel  Reconstruction kernel.
		/// @return One value per input position, sampled at map(x[i]).
		/// @note   The identity map reproduces the input; use it as the
		///         reference when building pitch or time-scale plots.
		static std::vector<Number> warp(const std::vector<Number>& x, const std::vector<Number>& y,
			const std::function<Number(Number)>& map, const KernelId kernel = KernelId::Lanczos3) {
			requireSamples(x, y, "Sampler::warp");
			if (x.size() < 2) {
				throw std::invalid_argument("Sampler::warp: at least two samples are required");
			}
			const Number step = uniformStep(x, "Sampler::warp");
			std::vector<Number> result;
			result.reserve(x.size());
			for (const Number& position : x) {
				result.push_back(sampleUniform(x, y, step, map(position), kernel));
			}
			return result;
		}

	private:
		/// @brief  Throws unless two parallel sample arrays have the same size.
		static void requireSamples(const std::vector<Number>& x, const std::vector<Number>& y,
			const char* operation) {
			if (x.size() != y.size()) {
				throw std::invalid_argument(operation);
			}
		}

		/// @brief  The step of a uniform position array.
		/// @param  x          Strictly increasing positions.
		/// @param  operation  Name used in the exception message.
		static Number uniformStep(const std::vector<Number>& x, const char* operation) {
			if (x.size() < 2) {
				throw std::invalid_argument(operation);
			}
			const Number step = (x.back() - x.front()) / static_cast<Number>(x.size() - 1);
			if (!(step > Number(0))) {
				throw std::invalid_argument(operation);
			}
			const Number tolerance = std::max(Number(1), std::abs(x.back())) * static_cast<Number>(1.0e-9);
			for (std::size_t index = 0; index < x.size(); ++index) {
				const Number expected = x.front() + static_cast<Number>(index) * step;
				if (std::abs(x[index] - expected) > tolerance) {
					throw std::invalid_argument(operation);
				}
			}
			return step;
		}

		/// @brief  Interpolates on an already validated uniform grid.
		static Number sampleUniform(const std::vector<Number>& x, const std::vector<Number>& y,
			const Number& step, const Number& query, const KernelId kernel) {
			if (query < x.front() || query > x.back()) {
				return Number(0);
			}

			const Number position = (query - x.front()) / step;
			const long long nearest = static_cast<long long>(std::llround(position));
			const Number gridTolerance = std::max(Number(1), std::abs(query))
				* static_cast<Number>(1.0e-12);
			if (nearest >= 0 && nearest < static_cast<long long>(x.size())
				&& std::abs(x[static_cast<std::size_t>(nearest)] - query) <= gridTolerance) {
				return y[static_cast<std::size_t>(nearest)];
			}

			const Number support = kernelSupport(kernel);
			const long long first = std::max<long long>(0,
				static_cast<long long>(std::ceil(position - support)));
			const long long last = std::min<long long>(static_cast<long long>(x.size()) - 1,
				static_cast<long long>(std::floor(position + support)));
			Number total = Number(0);
			Number weightTotal = Number(0);
			for (long long index = first; index <= last; ++index) {
				const Number weight = kernelValue(kernel, position - static_cast<Number>(index));
				total += weight * y[static_cast<std::size_t>(index)];
				weightTotal += weight;
			}
			return (std::abs(weightTotal) > std::numeric_limits<Number>::epsilon())
				? total / weightTotal : Number(0);
		}

		/// @brief  Symmetric, unit-DC low-pass weights built from a kernel.
		static std::vector<Number> lowPassWeights(const Number& cutoff, const std::size_t halfLength,
			const KernelId kernel = KernelId::WindowedSinc) {
			std::vector<Number> weights(2 * halfLength + 1, Number(0));
			Number total = Number(0);
			for (long long tap = -static_cast<long long>(halfLength);
				tap <= static_cast<long long>(halfLength); ++tap) {
				const Number weight = cutoff * kernelValue(kernel, cutoff * static_cast<Number>(tap));
				weights[static_cast<std::size_t>(tap + static_cast<long long>(halfLength))] = weight;
				total += weight;
			}
			if (std::abs(total) > std::numeric_limits<Number>::epsilon()) {
				for (Number& weight : weights) {
					weight /= total;
				}
			}
			return weights;
		}

		/// @brief  Zero-phase convolution with a symmetric kernel.
		static std::vector<Number> filterCentered(const std::vector<Number>& signal,
			const std::vector<Number>& weights) {
			const std::size_t half = weights.size() / 2;
			std::vector<Number> result(signal.size(), Number(0));
			for (std::size_t index = 0; index < signal.size(); ++index) {
				Number total = Number(0);
				for (std::size_t tap = 0; tap < weights.size(); ++tap) {
					const long long source = static_cast<long long>(index)
						+ static_cast<long long>(tap) - static_cast<long long>(half);
					if (source >= 0 && source < static_cast<long long>(signal.size())) {
						total += weights[tap] * signal[static_cast<std::size_t>(source)];
					}
				}
				result[index] = total;
			}
			return result;
		}

		/// @brief  Applies a low-pass filter built by lowPassWeights().
		static std::vector<Number> lowPass(const std::vector<Number>& signal, const Number& cutoff,
			const std::size_t halfLength, const KernelId kernel = KernelId::WindowedSinc) {
			return filterCentered(signal, lowPassWeights(cutoff, halfLength, kernel));
		}

		/// @brief  Applies a single-radix transform after checking the length.
		static void applyRadix(std::vector<Complex<Number>>& data, const std::size_t radix) {
			std::size_t remaining = data.size();
			if (remaining == 0) {
				return;
			}
			while (remaining % radix == 0) {
				remaining /= radix;
			}
			if (remaining != 1) {
				throw std::invalid_argument("Sampler: the sequence length is not a power of the requested radix");
			}
			FourierPlan<Number>::forSize(data.size()).forward(data);
		}
	};
}
