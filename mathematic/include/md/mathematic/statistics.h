/// @file   statistics.h
/// @brief  Descriptive statistics, Student t tests, permutation tests, bootstrap
///         intervals and multiple-comparison correction.
///
/// Everything is deterministic: every randomised routine takes an explicit seed.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace md::mathematic {

	/// @brief  The outcome of a hypothesis test.
	/// @tparam Number  Floating-point type used for the statistic.
	template<typename Number = double>
	struct TestResult {
		Number statistic = Number(0);			///< Test statistic, t or r depending on the test.
		Number pValue = Number(1);				///< Two-sided p value.
		Number degreesOfFreedom = Number(0);	///< Degrees of freedom, zero where not applicable.
	};

	/// @brief  A confidence interval.
	/// @tparam Number  Floating-point type used for the bounds.
	template<typename Number = double>
	struct Interval {
		Number lower = Number(0);	///< Lower bound.
		Number upper = Number(0);	///< Upper bound.
	};

	/// @brief  The arithmetic mean.
	/// @throws std::invalid_argument  When the sample is empty.
	template<typename Number = double>
	Number mean(const std::vector<Number>& samples) {
		if (samples.empty()) {
			throw std::invalid_argument("mean: the sample is empty");
		}
		Number total = Number(0);
		for (const Number& value : samples) {
			total += value;
		}
		return total / static_cast<Number>(samples.size());
	}

	/// @brief  The variance of a sample.
	/// @param  samples  Values.
	/// @param  unbiased  True for the n-1 denominator, false for n.
	/// @throws std::invalid_argument  When fewer values are supplied than the
	///         denominator requires.
	template<typename Number = double>
	Number variance(const std::vector<Number>& samples, const bool unbiased = true) {
		const std::size_t count = samples.size();
		if (count == 0 || (unbiased && count < 2)) {
			throw std::invalid_argument("variance: too few samples for the requested denominator");
		}
		const Number centre = mean(samples);
		Number total = Number(0);
		for (const Number& value : samples) {
			total += (value - centre) * (value - centre);
		}
		return total / static_cast<Number>(unbiased ? count - 1 : count);
	}

	/// @brief  The standard deviation of a sample.
	template<typename Number = double>
	Number standardDeviation(const std::vector<Number>& samples, const bool unbiased = true) {
		return static_cast<Number>(std::sqrt(variance(samples, unbiased)));
	}

	/// @brief  The standard error of the mean.
	template<typename Number = double>
	Number standardError(const std::vector<Number>& samples) {
		return standardDeviation(samples) / static_cast<Number>(std::sqrt(static_cast<Number>(samples.size())));
	}

	/// @brief  The median.
	/// @throws std::invalid_argument  When the sample is empty.
	template<typename Number = double>
	Number median(std::vector<Number> samples) {
		if (samples.empty()) {
			throw std::invalid_argument("median: the sample is empty");
		}
		std::sort(samples.begin(), samples.end());
		const std::size_t middle = samples.size() / 2;
		if (samples.size() % 2 == 1) {
			return samples[middle];
		}
		return (samples[middle - 1] + samples[middle]) / static_cast<Number>(2);
	}

	/// @brief  Pearson correlation of two equally long samples.
	/// @return The correlation in [-1, 1], or zero when a sample has no variance.
	/// @throws std::invalid_argument  When the sizes differ or are below two.
	template<typename Number = double>
	Number pearsonCorrelation(const std::vector<Number>& first, const std::vector<Number>& second) {
		if (first.size() != second.size() || first.size() < 2) {
			throw std::invalid_argument("pearsonCorrelation: the samples must be equally long and have at least two values");
		}
		const Number firstMean = mean(first);
		const Number secondMean = mean(second);
		Number covariance = Number(0);
		Number firstEnergy = Number(0);
		Number secondEnergy = Number(0);
		for (std::size_t index = 0; index < first.size(); ++index) {
			const Number left = first[index] - firstMean;
			const Number right = second[index] - secondMean;
			covariance += left * right;
			firstEnergy += left * left;
			secondEnergy += right * right;
		}
		const Number denominator = static_cast<Number>(std::sqrt(firstEnergy * secondEnergy));
		if (!(denominator > Number(0))) {
			return Number(0);
		}
		return covariance / denominator;
	}

	/// @brief  The regularized incomplete beta function I_x(a, b).
	/// @note   Continued-fraction evaluation (Lentz), symmetric in a and b.
	inline double regularizedIncompleteBeta(const double a, const double b, const double x) {
		if (!(x > 0.0)) {
			return 0.0;
		}
		if (x >= 1.0) {
			return 1.0;
		}

		const auto fraction = [](const double a, const double b, const double x) {
			constexpr int maxIterations = 200;
			constexpr double epsilon = 3.0e-14;
			constexpr double minimum = 1.0e-300;
			const double qab = a + b;
			const double qap = a + 1.0;
			const double qam = a - 1.0;
			double c = 1.0;
			double d = 1.0 - qab * x / qap;
			if (std::abs(d) < minimum) {
				d = minimum;
			}
			d = 1.0 / d;
			double h = d;
			for (int iteration = 1; iteration <= maxIterations; ++iteration) {
				const int doubled = 2 * iteration;

				const double firstTerm = iteration * (b - iteration) * x
					/ ((qam + doubled) * (a + doubled));
				d = 1.0 + firstTerm * d;
				if (std::abs(d) < minimum) {
					d = minimum;
				}
				c = 1.0 + firstTerm / c;
				if (std::abs(c) < minimum) {
					c = minimum;
				}
				d = 1.0 / d;
				h *= d * c;

				const double secondTerm = -(a + iteration) * (qab + iteration) * x
					/ ((a + doubled) * (qap + doubled));
				d = 1.0 + secondTerm * d;
				if (std::abs(d) < minimum) {
					d = minimum;
				}
				c = 1.0 + secondTerm / c;
				if (std::abs(c) < minimum) {
					c = minimum;
				}
				d = 1.0 / d;
				const double delta = d * c;
				h *= delta;
				if (std::abs(delta - 1.0) < epsilon) {
					break;
				}
			}
			return h;
		};

		const double logFront = std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b)
			+ a * std::log(x) + b * std::log(1.0 - x);
		const double front = std::exp(logFront);
		if (x < (a + 1.0) / (a + b + 2.0)) {
			return front * fraction(a, b, x) / a;
		}
		return 1.0 - front * fraction(b, a, 1.0 - x) / b;
	}

	/// @brief  The two-sided p value of a t statistic.
	/// @param  t           Test statistic.
	/// @param  degrees     Degrees of freedom; must be positive.
	/// @return P(|T| >= |t|).
	/// @throws std::invalid_argument  When the degrees of freedom are not positive.
	inline double studentTwoSidedP(const double t, const double degrees) {
		if (!(degrees > 0.0)) {
			throw std::invalid_argument("studentTwoSidedP: the degrees of freedom must be positive");
		}
		const double x = degrees / (degrees + t * t);
		return regularizedIncompleteBeta(0.5 * degrees, 0.5, x);
	}

	/// @brief  A one-sample t test against a reference mean.
	/// @throws std::invalid_argument  When fewer than two samples are supplied.
	template<typename Number = double>
	TestResult<Number> oneSampleTTest(const std::vector<Number>& samples, const Number& reference = Number(0)) {
		if (samples.size() < 2) {
			throw std::invalid_argument("oneSampleTTest: at least two samples are required");
		}
		const Number centre = mean(samples);
		const Number error = standardError(samples);
		TestResult<Number> result;
		result.degreesOfFreedom = static_cast<Number>(samples.size() - 1);
		if (!(error > Number(0))) {
			result.statistic = Number(0);
			result.pValue = (centre == reference) ? Number(1) : Number(0);
			return result;
		}
		result.statistic = (centre - reference) / error;
		result.pValue = static_cast<Number>(studentTwoSidedP(static_cast<double>(result.statistic),
			static_cast<double>(result.degreesOfFreedom)));
		return result;
	}

	/// @brief  A two-sample t test, Welch by default.
	/// @param  first   First sample.
	/// @param  second  Second sample.
	/// @param  welch   True for unequal variances, false for the pooled variant.
	/// @throws std::invalid_argument  When either sample has fewer than two values.
	template<typename Number = double>
	TestResult<Number> twoSampleTTest(const std::vector<Number>& first, const std::vector<Number>& second,
		const bool welch = true) {
		if (first.size() < 2 || second.size() < 2) {
			throw std::invalid_argument("twoSampleTTest: each sample needs at least two values");
		}
		TestResult<Number> result;
		const Number firstVariance = variance(first);
		const Number secondVariance = variance(second);
		const Number firstSize = static_cast<Number>(first.size());
		const Number secondSize = static_cast<Number>(second.size());

		if (welch) {
			const Number firstShare = firstVariance / firstSize;
			const Number secondShare = secondVariance / secondSize;
			const Number denominator = static_cast<Number>(std::sqrt(firstShare + secondShare));
			const Number numerator = firstShare + secondShare;
			const Number squares = firstShare * firstShare / (firstSize - Number(1))
				+ secondShare * secondShare / (secondSize - Number(1));
			result.statistic = denominator > Number(0)
				? (mean(first) - mean(second)) / denominator
				: Number(0);
			result.degreesOfFreedom = squares > Number(0) ? (numerator * numerator) / squares : Number(1);
		}
		else {
			const Number pooled = ((firstSize - Number(1)) * firstVariance
				+ (secondSize - Number(1)) * secondVariance) / (firstSize + secondSize - Number(2));
			const Number denominator = static_cast<Number>(std::sqrt(pooled * (Number(1) / firstSize
				+ Number(1) / secondSize)));
			result.statistic = denominator > Number(0)
				? (mean(first) - mean(second)) / denominator
				: Number(0);
			result.degreesOfFreedom = firstSize + secondSize - Number(2);
		}
		result.pValue = static_cast<Number>(studentTwoSidedP(static_cast<double>(result.statistic),
			static_cast<double>(result.degreesOfFreedom)));
		return result;
	}

	/// @brief  A paired t test on the differences of two equally long samples.
	/// @throws std::invalid_argument  When the sizes differ or are below two.
	template<typename Number = double>
	TestResult<Number> pairedTTest(const std::vector<Number>& first, const std::vector<Number>& second) {
		if (first.size() != second.size() || first.size() < 2) {
			throw std::invalid_argument("pairedTTest: the samples must be equally long and have at least two values");
		}
		std::vector<Number> differences(first.size());
		for (std::size_t index = 0; index < first.size(); ++index) {
			differences[index] = first[index] - second[index];
		}
		return oneSampleTTest(differences, Number(0));
	}

	/// @brief  Cohen's d effect size for two samples.
	/// @return The mean difference over the pooled standard deviation.
	template<typename Number = double>
	Number cohensD(const std::vector<Number>& first, const std::vector<Number>& second) {
		if (first.size() < 2 || second.size() < 2) {
			throw std::invalid_argument("cohensD: each sample needs at least two values");
		}
		const Number firstVariance = variance(first);
		const Number secondVariance = variance(second);
		const Number pooled = static_cast<Number>(std::sqrt(
			((Number(first.size()) - Number(1)) * firstVariance
				+ (Number(second.size()) - Number(1)) * secondVariance)
			/ (Number(first.size()) + Number(second.size()) - Number(2))));
		if (!(pooled > Number(0))) {
			return Number(0);
		}
		return (mean(first) - mean(second)) / pooled;
	}

	/// @brief  A two-sided permutation test of the difference of means.
	/// @param  first      First sample.
	/// @param  second     Second sample.
	/// @param  resamples  Number of permutations; 2000 is a reasonable default.
	/// @param  seed       Random seed, so a report can be reproduced.
	/// @return The observed difference as the statistic, and the fraction of
	///         permutations at least as extreme, with the add-one correction.
	/// @throws std::invalid_argument  When a sample is empty or no resamples are asked for.
	template<typename Number = double>
	TestResult<Number> permutationTestDifference(const std::vector<Number>& first,
		const std::vector<Number>& second, const std::size_t resamples = 2000,
		const std::uint32_t seed = 20261112u) {
		if (first.empty() || second.empty()) {
			throw std::invalid_argument("permutationTestDifference: both samples must be non-empty");
		}
		if (resamples == 0) {
			throw std::invalid_argument("permutationTestDifference: at least one resample is required");
		}

		const Number observed = mean(first) - mean(second);
		std::vector<Number> pooled;
		pooled.reserve(first.size() + second.size());
		pooled.insert(pooled.end(), first.begin(), first.end());
		pooled.insert(pooled.end(), second.begin(), second.end());

		std::mt19937 generator(seed);
		const Number magnitude = std::abs(observed);
		std::size_t extreme = 0;
		std::vector<Number> shuffled(pooled.size());
		for (std::size_t iteration = 0; iteration < resamples; ++iteration) {
			shuffled = pooled;
			for (std::size_t index = shuffled.size(); index > 1; --index) {
				std::uniform_int_distribution<std::size_t> pick(0, index - 1);
				std::swap(shuffled[index - 1], shuffled[pick(generator)]);
			}
			Number firstTotal = Number(0);
			for (std::size_t index = 0; index < first.size(); ++index) {
				firstTotal += shuffled[index];
			}
			Number secondTotal = Number(0);
			for (std::size_t index = first.size(); index < shuffled.size(); ++index) {
				secondTotal += shuffled[index];
			}
			const Number difference = firstTotal / Number(first.size()) - secondTotal / Number(second.size());
			if (std::abs(difference) >= magnitude) {
				++extreme;
			}
		}

		TestResult<Number> result;
		result.statistic = observed;
		result.pValue = static_cast<Number>(extreme + 1) / static_cast<Number>(resamples + 1);
		return result;
	}

	/// @brief  A two-sided permutation test of the Pearson correlation.
	/// @param  x          First sample.
	/// @param  y          Second sample, equally long.
	/// @param  resamples  Number of permutations.
	/// @param  seed       Random seed.
	/// @throws std::invalid_argument  When the sizes differ or are below two.
	template<typename Number = double>
	TestResult<Number> permutationTestCorrelation(const std::vector<Number>& x, const std::vector<Number>& y,
		const std::size_t resamples = 2000, const std::uint32_t seed = 20261112u) {
		if (x.size() != y.size() || x.size() < 2) {
			throw std::invalid_argument("permutationTestCorrelation: the samples must be equally long and have at least two values");
		}
		if (resamples == 0) {
			throw std::invalid_argument("permutationTestCorrelation: at least one resample is required");
		}

		TestResult<Number> result;
		result.statistic = pearsonCorrelation(x, y);
		const Number magnitude = std::abs(result.statistic);
		std::vector<Number> shuffled(y);
		std::mt19937 generator(seed);
		std::size_t extreme = 0;
		for (std::size_t iteration = 0; iteration < resamples; ++iteration) {
			shuffled = y;
			for (std::size_t index = shuffled.size(); index > 1; --index) {
				std::uniform_int_distribution<std::size_t> pick(0, index - 1);
				std::swap(shuffled[index - 1], shuffled[pick(generator)]);
			}
			if (std::abs(pearsonCorrelation(x, shuffled)) >= magnitude) {
				++extreme;
			}
		}
		result.pValue = static_cast<Number>(extreme + 1) / static_cast<Number>(resamples + 1);
		return result;
	}

	/// @brief  A percentile bootstrap interval for the mean.
	/// @param  samples    Values to resample.
	/// @param  level      Confidence level, 0.95 by default.
	/// @param  resamples  Number of bootstrap replicates.
	/// @param  seed       Random seed.
	/// @throws std::invalid_argument  When the sample is empty, the level is outside
	///         (0, 1) or no resamples are asked for.
	template<typename Number = double>
	Interval<Number> bootstrapMeanInterval(const std::vector<Number>& samples,
		const Number& level = static_cast<Number>(0.95), const std::size_t resamples = 2000,
		const std::uint32_t seed = 20261112u) {
		if (samples.empty()) {
			throw std::invalid_argument("bootstrapMeanInterval: the sample is empty");
		}
		if (!(level > Number(0)) || !(level < Number(1))) {
			throw std::invalid_argument("bootstrapMeanInterval: the level must lie strictly inside (0, 1)");
		}
		if (resamples == 0) {
			throw std::invalid_argument("bootstrapMeanInterval: at least one resample is required");
		}

		std::mt19937 generator(seed);
		std::uniform_int_distribution<std::size_t> pick(0, samples.size() - 1);
		std::vector<Number> means;
		means.reserve(resamples);
		for (std::size_t iteration = 0; iteration < resamples; ++iteration) {
			Number total = Number(0);
			for (std::size_t draw = 0; draw < samples.size(); ++draw) {
				total += samples[pick(generator)];
			}
			means.push_back(total / static_cast<Number>(samples.size()));
		}
		std::sort(means.begin(), means.end());

		const Number alpha = (Number(1) - level) / Number(2);
		const std::size_t lowerIndex = static_cast<std::size_t>(alpha * static_cast<Number>(resamples - 1));
		const std::size_t upperIndex = static_cast<std::size_t>((Number(1) - alpha) * static_cast<Number>(resamples - 1));
		Interval<Number> interval;
		interval.lower = means[std::min(lowerIndex, means.size() - 1)];
		interval.upper = means[std::min(upperIndex, means.size() - 1)];
		return interval;
	}

	/// @brief  Multiplies every p value by the number of tests, clamped at one.
	template<typename Number = double>
	std::vector<Number> adjustBonferroni(const std::vector<Number>& pValues) {
		std::vector<Number> adjusted;
		adjusted.reserve(pValues.size());
		const Number count = static_cast<Number>(pValues.size());
		for (const Number& value : pValues) {
			adjusted.push_back(std::min(value * count, Number(1)));
		}
		return adjusted;
	}

	/// @brief  Benjamini-Hochberg false discovery rate adjustment.
	/// @param  pValues  Raw p values.
	/// @return Adjusted values in the input order, monotone in the sorted order.
	template<typename Number = double>
	std::vector<Number> adjustBenjaminiHochberg(const std::vector<Number>& pValues) {
		const std::size_t count = pValues.size();
		std::vector<std::size_t> order(count);
		std::iota(order.begin(), order.end(), std::size_t{ 0 });
		std::sort(order.begin(), order.end(), [&pValues](const std::size_t left, const std::size_t right) {
			return pValues[left] < pValues[right];
		});

		std::vector<Number> adjusted(count, Number(1));
		Number running = Number(1);
		for (std::size_t rank = count; rank-- > 0;) {
			const Number value = pValues[order[rank]] * static_cast<Number>(count)
				/ static_cast<Number>(rank + 1);
			running = std::min(running, std::min(value, Number(1)));
			adjusted[order[rank]] = running;
		}
		return adjusted;
	}
}
