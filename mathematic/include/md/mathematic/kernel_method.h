/// @file   kernel_method.h
/// @brief  The kernel machines that sit on top of kernel.h: kernel ridge
///         regression, Gaussian process regression with a predictive variance,
///         and a one-versus-rest kernel classifier.
///
/// All three reduce to the same linear algebra: build the Gram matrix, add a ridge
/// to its diagonal, solve once, and keep the coefficients. Nothing here restates a
/// kernel formula; the shapes come from kernel.h.

#pragma once

#include <md/mathematic/kernel.h>
#include <md/mathematic/matrix.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace md::mathematic {

	/// @brief  A fitted kernel ridge regression.
	///
	/// The model predicts
	///   f(x) = mean(y) + sum_i alpha_i k(x, x_i),
	/// which is ridge regression carried out in the feature space the kernel
	/// induces, without ever forming that feature space.
	///
	/// @tparam Number  Floating-point type used for the values.
	template<typename Number = double>
	struct KernelRidgeModel {
		KernelType type = KernelType::Gaussian;		///< Kernel shape.
		KernelParameters<Number> parameters;		///< Kernel shape parameters.
		std::vector<std::vector<Number>> support;	///< Training points.
		std::vector<Number> coefficients;			///< One weight per training point.
		Number intercept = Number(0);				///< Mean of the targets.

		/// @brief  Predicts one point.
		/// @throws std::invalid_argument  When the point length does not match.
		Number predict(const std::vector<Number>& point) const {
			if (!support.empty() && point.size() != support.front().size()) {
				throw std::invalid_argument("KernelRidgeModel::predict: the point has the wrong length");
			}
			Number total = intercept;
			for (std::size_t index = 0; index < support.size(); ++index) {
				total += coefficients[index] * kernelValue(type, point, support[index], parameters);
			}
			return total;
		}
	};

	/// @brief  Builds the regularised Gram system shared by every kernel method.
	/// @param  points      Training points.
	/// @param  type        Kernel shape.
	/// @param  parameters  Kernel shape parameters.
	/// @param  ridge       Value added to the diagonal; must not be negative.
	/// @return K + ridge * I.
	/// @throws std::invalid_argument  When the set is empty, ragged or the ridge is negative.
	template<typename Number = double>
	Matrix<Number> regularisedGram(const std::vector<std::vector<Number>>& points, const KernelType type,
		const KernelParameters<Number>& parameters, const Number& ridge) {
		if (points.empty()) {
			throw std::invalid_argument("regularisedGram: the point set is empty");
		}
		if (ridge < Number(0)) {
			throw std::invalid_argument("regularisedGram: the ridge must not be negative");
		}
		Matrix<Number> gram = gramMatrix(type, points, parameters);
		for (std::size_t index = 0; index < points.size(); ++index) {
			gram(index, index) += ridge;
		}
		return gram;
	}

	/// @brief  Builds the Gaussian process system signalVariance * K + noiseVariance * I.
	/// @param  points          Training points.
	/// @param  type            Kernel shape.
	/// @param  parameters      Kernel shape parameters.
	/// @param  signalVariance  Scale of the latent function; absorbs the unit of the data.
	/// @param  noiseVariance   Observation noise variance.
	/// @return The system matrix.
	/// @throws std::invalid_argument  When either variance is not positive.
	/// @note   Biological recordings arrive in microvolts, millivolts or raw counts, so
	///         the signal scale has to be a free parameter rather than an assumption.
	template<typename Number = double>
	Matrix<Number> scaledGram(const std::vector<std::vector<Number>>& points, const KernelType type,
		const KernelParameters<Number>& parameters, const Number& signalVariance,
		const Number& noiseVariance) {
		if (!(signalVariance > Number(0)) || !(noiseVariance > Number(0))) {
			throw std::invalid_argument("scaledGram: the signal and noise variances must be positive");
		}
		Matrix<Number> gram = gramMatrix(type, points, parameters);
		for (std::size_t row = 0; row < points.size(); ++row) {
			for (std::size_t column = 0; column < points.size(); ++column) {
				gram(row, column) *= signalVariance;
			}
			gram(row, row) += noiseVariance;
		}
		return gram;
	}

	/// @brief  Fits a kernel ridge regression.
	/// @param  points      Training points.
	/// @param  targets     One target value per point.
	/// @param  type        Kernel shape.
	/// @param  parameters  Kernel shape parameters.
	/// @param  ridge       Regularisation strength; 1e-6 interpolates, larger smooths.
	/// @return The fitted model.
	/// @throws std::invalid_argument  When the counts disagree, the set is empty or
	///         the ridge is negative.
	/// @throws std::domain_error  When the regularised system is singular.
	template<typename Number = double>
	KernelRidgeModel<Number> fitKernelRidge(const std::vector<std::vector<Number>>& points,
		const std::vector<Number>& targets, const KernelType type,
		const KernelParameters<Number>& parameters = KernelParameters<Number>{},
		const Number& ridge = static_cast<Number>(1e-6)) {
		if (points.size() != targets.size()) {
			throw std::invalid_argument("fitKernelRidge: the point and target counts differ");
		}

		KernelRidgeModel<Number> model;
		model.type = type;
		model.parameters = parameters;
		model.support = points;

		Number average = Number(0);
		for (const Number& value : targets) {
			average += value;
		}
		average /= static_cast<Number>(targets.size());
		model.intercept = average;

		std::vector<Number> centred(targets.size(), Number(0));
		for (std::size_t index = 0; index < targets.size(); ++index) {
			centred[index] = targets[index] - average;
		}

		const Matrix<Number> system = regularisedGram(points, type, parameters, ridge);
		model.coefficients = solve(system, centred);
		return model;
	}

	/// @brief  A fitted Gaussian process regression.
	///
	/// The predictive mean equals kernel ridge regression with a ridge equal to the
	/// noise variance; the addition is the predictive variance, which says how much
	/// to trust a prediction away from the training points.
	///
	/// @tparam Number  Floating-point type used for the values.
	template<typename Number = double>
	struct GaussianProcessModel {
		KernelType type = KernelType::Gaussian;		///< Kernel shape.
		KernelParameters<Number> parameters;		///< Kernel shape parameters.
		std::vector<std::vector<Number>> support;	///< Training points.
		std::vector<Number> coefficients;			///< (K + noise I)^-1 (y - mean).
		Number intercept = Number(0);				///< Mean of the targets.
		Number noiseVariance = static_cast<Number>(1e-6);	///< Observation noise variance.
		Number signalVariance = static_cast<Number>(1);		///< Scale of the latent function.

		/// @brief  The posterior mean at one point.
		/// @throws std::invalid_argument  When the point length does not match.
		Number predict(const std::vector<Number>& point) const {
			if (!support.empty() && point.size() != support.front().size()) {
				throw std::invalid_argument("GaussianProcessModel::predict: the point has the wrong length");
			}
			Number total = intercept;
			for (std::size_t index = 0; index < support.size(); ++index) {
				total += coefficients[index] * kernelValue(type, point, support[index], parameters);
			}
			return total;
		}

		/// @brief  The posterior variance at one point, excluding observation noise.
		/// @throws std::invalid_argument  When the point length does not match.
		/// @throws std::domain_error  When the system cannot be solved.
		Number predictiveVariance(const std::vector<Number>& point) const {
			if (support.empty()) {
				throw std::invalid_argument("GaussianProcessModel::predictiveVariance: the model is empty");
			}
			const Matrix<Number> system = scaledGram(support, type, parameters, signalVariance, noiseVariance);
			std::vector<Number> cross(support.size(), Number(0));
			for (std::size_t index = 0; index < support.size(); ++index) {
				cross[index] = kernelValue(type, point, support[index], parameters);
			}
			const std::vector<Number> weights = solve(system, cross);

			Number quadratic = Number(0);
			for (std::size_t index = 0; index < support.size(); ++index) {
				quadratic += cross[index] * weights[index];
			}
			const Number prior = signalVariance * kernelValue(type, point, point, parameters);
			// Rounding can push the difference a hair below zero at a training point.
			return std::max(prior - quadratic, Number(0));
		}
	};

	/// @brief  Fits a Gaussian process regression.
	/// @param  points        Training points.
	/// @param  targets       One target value per point.
	/// @param  type          Kernel shape.
	/// @param  parameters    Kernel shape parameters.
	/// @param  noiseVariance Observation noise variance; must be positive.
	/// @return The fitted model.
	/// @throws std::invalid_argument  When the counts disagree, the set is empty or
	///         the noise variance is not positive.
	/// @throws std::domain_error  When the system is singular.
	template<typename Number = double>
	GaussianProcessModel<Number> fitGaussianProcess(const std::vector<std::vector<Number>>& points,
		const std::vector<Number>& targets, const KernelType type,
		const KernelParameters<Number>& parameters = KernelParameters<Number>{},
		const Number& noiseVariance = static_cast<Number>(1e-6),
		const Number& signalVariance = static_cast<Number>(1)) {
		if (points.size() != targets.size()) {
			throw std::invalid_argument("fitGaussianProcess: the point and target counts differ");
		}
		if (!(noiseVariance > Number(0)) || !(signalVariance > Number(0))) {
			throw std::invalid_argument("fitGaussianProcess: the variances must be positive");
		}

		GaussianProcessModel<Number> model;
		model.type = type;
		model.parameters = parameters;
		model.support = points;
		model.noiseVariance = noiseVariance;
		model.signalVariance = signalVariance;

		Number average = Number(0);
		for (const Number& value : targets) {
			average += value;
		}
		average /= static_cast<Number>(targets.size());
		model.intercept = average;

		std::vector<Number> centred(targets.size(), Number(0));
		for (std::size_t index = 0; index < targets.size(); ++index) {
			centred[index] = targets[index] - average;
		}

		const Matrix<Number> system = scaledGram(points, type, parameters, signalVariance, noiseVariance);
		model.coefficients = solve(system, centred);
		return model;
	}

	/// @brief  A one-versus-rest kernel classifier built on kernel ridge regression.
	///
	/// @tparam Number  Floating-point type used for the values.
	template<typename Number = double>
	class KernelClassifier {
	private:
		std::vector<std::size_t> labels;				///< Class labels, ascending.
		std::vector<KernelRidgeModel<Number>> models;	///< One regression per class.

	public:
		/// @brief  Fits one ridge regression per class on a one-hot target.
		/// @param  points      Training points.
		/// @param  targets     One class label per point.
		/// @param  type        Kernel shape.
		/// @param  parameters  Kernel shape parameters.
		/// @param  ridge       Regularisation strength.
		/// @throws std::invalid_argument  When the counts disagree or only one class is present.
		/// @throws std::domain_error  When a class system is singular.
		void fit(const std::vector<std::vector<Number>>& points, const std::vector<std::size_t>& targets,
			const KernelType type, const KernelParameters<Number>& parameters = KernelParameters<Number>{},
			const Number& ridge = static_cast<Number>(1e-3)) {
			if (points.size() != targets.size()) {
				throw std::invalid_argument("KernelClassifier::fit: the point and label counts differ");
			}
			if (points.empty()) {
				throw std::invalid_argument("KernelClassifier::fit: the training set is empty");
			}

			labels = targets;
			std::sort(labels.begin(), labels.end());
			labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
			if (labels.size() < 2) {
				throw std::invalid_argument("KernelClassifier::fit: at least two classes are required");
			}

			models.clear();
			models.reserve(labels.size());
			for (const std::size_t label : labels) {
				std::vector<Number> indicator(points.size(), Number(0));
				for (std::size_t index = 0; index < points.size(); ++index) {
					indicator[index] = (targets[index] == label) ? Number(1) : Number(0);
				}
				models.push_back(fitKernelRidge(points, indicator, type, parameters, ridge));
			}
		}

		/// @brief  The class labels in ascending order.
		const std::vector<std::size_t>& classes() const {
			return labels;
		}

		/// @brief  The score of every class at one point.
		/// @throws std::logic_error  When the model has not been fitted.
		std::vector<Number> scores(const std::vector<Number>& point) const {
			if (models.empty()) {
				throw std::logic_error("KernelClassifier::scores: the model is not fitted");
			}
			std::vector<Number> result(models.size(), Number(0));
			for (std::size_t index = 0; index < models.size(); ++index) {
				result[index] = models[index].predict(point);
			}
			return result;
		}

		/// @brief  The class with the highest score.
		std::size_t predict(const std::vector<Number>& point) const {
			const std::vector<Number> values = scores(point);
			const auto best = std::max_element(values.begin(), values.end());
			return labels[static_cast<std::size_t>(best - values.begin())];
		}
	};

	/// @brief  The log marginal likelihood of a Gaussian process.
	///
	/// Computes   -1/2 y^T K^-1 y - 1/2 log|K| - n/2 log(2 pi)   on the centred
	/// targets, with K the regularised Gram matrix. The first term rewards fit, the
	/// second penalises complexity, and their balance is what selects hyperparameters.
	///
	/// @param  points        Training points.
	/// @param  targets       One target value per point.
	/// @param  type          Kernel shape.
	/// @param  parameters    Kernel shape parameters.
	/// @param  noiseVariance Observation noise variance; must be positive.
	/// @return The log evidence.
	/// @throws std::invalid_argument  When the counts disagree, the set is empty or
	///         the noise variance is not positive.
	/// @throws std::domain_error  When the Gram matrix is not positive definite.
	template<typename Number = double>
	Number logMarginalLikelihood(const std::vector<std::vector<Number>>& points,
		const std::vector<Number>& targets, const KernelType type,
		const KernelParameters<Number>& parameters, const Number& noiseVariance,
		const Number& signalVariance = static_cast<Number>(1)) {
		if (points.size() != targets.size()) {
			throw std::invalid_argument("logMarginalLikelihood: the point and target counts differ");
		}
		if (points.empty()) {
			throw std::invalid_argument("logMarginalLikelihood: the point set is empty");
		}
		if (!(noiseVariance > Number(0)) || !(signalVariance > Number(0))) {
			throw std::invalid_argument("logMarginalLikelihood: the variances must be positive");
		}

		Number average = Number(0);
		for (const Number& value : targets) {
			average += value;
		}
		average /= static_cast<Number>(targets.size());
		std::vector<Number> centred(targets.size(), Number(0));
		for (std::size_t index = 0; index < targets.size(); ++index) {
			centred[index] = targets[index] - average;
		}

		const Matrix<Number> system = scaledGram(points, type, parameters, signalVariance, noiseVariance);
		const Matrix<Number> factor = cholesky(system);
		const std::vector<Number> weights = solve(system, centred);

		Number quadratic = Number(0);
		for (std::size_t index = 0; index < centred.size(); ++index) {
			quadratic += centred[index] * weights[index];
		}
		Number logDeterminant = Number(0);
		for (std::size_t index = 0; index < factor.rows(); ++index) {
			logDeterminant += Number(2) * static_cast<Number>(std::log(factor(index, index)));
		}
		const Number count = static_cast<Number>(targets.size());
		return Number(-0.5) * quadratic - Number(0.5) * logDeterminant
			- Number(0.5) * count * static_cast<Number>(std::log(DoublePi<Number>));
	}

	/// @brief  Hyperparameters chosen by maximising the log evidence.
	///
	/// @tparam Number  Floating-point type used for the values.
	template<typename Number = double>
	struct GaussianProcessHyperparameters {
		KernelParameters<Number> parameters;	///< Kernel parameters at the optimum.
		Number noiseVariance = static_cast<Number>(1e-6);	///< Noise variance at the optimum.
		Number signalVariance = static_cast<Number>(1);		///< Signal variance at the optimum.
		Number evidence = Number(0);			///< Log marginal likelihood there.
	};

	/// @brief  Selects length scale, signal variance and noise variance by coordinate ascent.
	/// @param  points          Training points.
	/// @param  targets         One target value per point.
	/// @param  type            Kernel shape.
	/// @param  initial         Starting kernel parameters; its length scale is the one varied.
	/// @param  noiseVariance   Starting noise variance.
	/// @param  signalVariance  Starting signal variance; a non-positive value means
	///                         "start from the variance of the targets", which is the
	///                         scale-free choice for biological recordings.
	/// @param  rounds          Coordinate sweeps; each tries factor-of-two steps in the
	///                         length scale and factor-of-ten steps in both variances.
	/// @return The best parameters found and the evidence there.
	/// @note   A grid sweep is used rather than a gradient because the evidence costs
	///         one Cholesky per candidate and a grid cannot diverge. It finds a good
	///         neighbourhood, not the exact maximum.
	template<typename Number = double>
	GaussianProcessHyperparameters<Number> optimiseGaussianProcess(
		const std::vector<std::vector<Number>>& points, const std::vector<Number>& targets,
		const KernelType type, KernelParameters<Number> initial,
		Number noiseVariance = static_cast<Number>(1e-3), Number signalVariance = Number(-1),
		const unsigned rounds = 3) {
		if (!(initial.lengthScale > Number(0)) || !(noiseVariance > Number(0))) {
			throw std::invalid_argument("optimiseGaussianProcess: the length scale and noise must be positive");
		}
		if (!(signalVariance > Number(0))) {
			// Fall back to the scale of the data itself.
			Number average = Number(0);
			for (const Number& value : targets) {
				average += value;
			}
			average /= static_cast<Number>(std::max<std::size_t>(targets.size(), 1));
			Number spread = Number(0);
			for (const Number& value : targets) {
				spread += (value - average) * (value - average);
			}
			spread /= static_cast<Number>(std::max<std::size_t>(targets.size(), 1));
			signalVariance = (spread > Number(0)) ? spread : Number(1);
		}

		GaussianProcessHyperparameters<Number> best;
		best.parameters = initial;
		best.noiseVariance = noiseVariance;
		best.signalVariance = signalVariance;
		best.evidence = logMarginalLikelihood(points, targets, type, initial, noiseVariance, signalVariance);

		const Number lengthFactors[2] = { Number(0.5), Number(2) };
		const Number spreadFactors[2] = { Number(0.1), Number(10) };
		for (unsigned round = 0; round < rounds; ++round) {
			for (const Number factor : lengthFactors) {
				KernelParameters<Number> candidate = best.parameters;
				candidate.lengthScale *= factor;
				const Number value = logMarginalLikelihood(points, targets, type, candidate,
					best.noiseVariance, best.signalVariance);
				if (value > best.evidence) {
					best.evidence = value;
					best.parameters = candidate;
				}
			}
			for (const Number factor : spreadFactors) {
				const Number candidate = best.signalVariance * factor;
				const Number value = logMarginalLikelihood(points, targets, type, best.parameters,
					best.noiseVariance, candidate);
				if (value > best.evidence) {
					best.evidence = value;
					best.signalVariance = candidate;
				}
			}
			for (const Number factor : spreadFactors) {
				const Number candidate = best.noiseVariance * factor;
				const Number value = logMarginalLikelihood(points, targets, type, best.parameters,
					candidate, best.signalVariance);
				if (value > best.evidence) {
					best.evidence = value;
					best.noiseVariance = candidate;
				}
			}
		}
		return best;
	}
}
