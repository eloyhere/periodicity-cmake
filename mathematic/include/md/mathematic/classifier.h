/// @file   classifier.h
/// @brief  Gaussian linear discriminant analysis and the stratified cross
///         validation that reports whether a decoding result means anything.
///

#pragma once

#include <md/mathematic/matrix.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace md::mathematic {

	/// @brief  The outcome of a cross-validation run.
	///
	/// @tparam Number  Floating-point type used for the scores.
	template<typename Number = double>
	struct CrossValidationResult {
		Number accuracy = Number(0);			///< Overall accuracy, in [0, 1].
		Number kappa = Number(0);				///< Cohen's kappa, chance corrected.
		std::vector<Number> foldAccuracy;		///< Accuracy of each fold.
		Matrix<Number> confusion;				///< Rows are true labels, columns predicted.
		std::size_t samples = 0;				///< Number of evaluated samples.
	};

	/// @brief  Linear discriminant analysis with a shared, shrunk covariance.
	///
	/// The decision rule is the Gaussian discriminant
	///   delta_c(x) = x^T S^-1 mu_c - mu_c^T S^-1 mu_c / 2 + log(prior_c),
	/// which reduces to nearest centroid when the within-class scatter is isotropic.
	///
	/// @tparam Number  Floating-point type used for the features.
	template<typename Number = double>
	class LinearDiscriminant {
	private:
		std::vector<std::size_t> labels;		///< Class labels, ascending.
		std::vector<Number> priors;				///< Class priors.
		std::vector<std::vector<Number>> means;	///< Class means, same order as labels.
		std::vector<std::vector<Number>> weights;	///< S^-1 mu_c for each class.
		std::vector<Number> offsets;			///< -mu_c^T S^-1 mu_c / 2 + log prior.
		std::size_t featureCount = 0;			///< Expected feature length.

	public:
		/// @brief  Fits the classifier.
		/// @param  features    One feature vector per sample.
		/// @param  targets     One class label per sample.
		/// @param  shrinkage   Ridge added to the covariance as a fraction of its mean
		///                     diagonal; 1e-3 keeps small sample counts usable.
		/// @throws std::invalid_argument  When the sizes disagree, a sample is empty or
		///         only one class is present.
		void fit(const std::vector<std::vector<Number>>& features, const std::vector<std::size_t>& targets,
			const Number& shrinkage = static_cast<Number>(1e-3)) {
			if (features.size() != targets.size()) {
				throw std::invalid_argument("LinearDiscriminant::fit: the feature and label counts differ");
			}
			if (features.empty()) {
				throw std::invalid_argument("LinearDiscriminant::fit: the training set is empty");
			}
			featureCount = features.front().size();
			if (featureCount == 0) {
				throw std::invalid_argument("LinearDiscriminant::fit: a feature vector is empty");
			}

			labels = targets;
			std::sort(labels.begin(), labels.end());
			labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
			if (labels.size() < 2) {
				throw std::invalid_argument("LinearDiscriminant::fit: at least two classes are required");
			}

			const std::size_t classes = labels.size();
			const std::size_t dimensions = featureCount;
			const std::size_t count = features.size();

			means.assign(classes, std::vector<Number>(dimensions, Number(0)));
			std::vector<std::size_t> sizes(classes, 0);
			for (std::size_t index = 0; index < count; ++index) {
				if (features[index].size() != dimensions) {
					throw std::invalid_argument("LinearDiscriminant::fit: the feature lengths differ");
				}
				const auto position = std::lower_bound(labels.begin(), labels.end(), targets[index]);
				const std::size_t label = static_cast<std::size_t>(position - labels.begin());
				++sizes[label];
				for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
					means[label][dimension] += features[index][dimension];
				}
			}
			for (std::size_t label = 0; label < classes; ++label) {
				if (sizes[label] == 0) {
					throw std::invalid_argument("LinearDiscriminant::fit: a class has no samples");
				}
				for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
					means[label][dimension] /= static_cast<Number>(sizes[label]);
				}
			}

			// Pooled within-class covariance.
			Matrix<Number> covariance(dimensions, dimensions, Number(0));
			for (std::size_t index = 0; index < count; ++index) {
				const auto position = std::lower_bound(labels.begin(), labels.end(), targets[index]);
				const std::size_t label = static_cast<std::size_t>(position - labels.begin());
				for (std::size_t row = 0; row < dimensions; ++row) {
					const Number left = features[index][row] - means[label][row];
					for (std::size_t column = row; column < dimensions; ++column) {
						const Number value = left * (features[index][column] - means[label][column]);
						covariance(row, column) += value;
						covariance(column, row) += value;
					}
				}
			}
			const Number denominator = static_cast<Number>(count - classes);
			if (!(denominator > Number(0))) {
				throw std::invalid_argument("LinearDiscriminant::fit: too few samples for the class count");
			}
			Number trace = Number(0);
			for (std::size_t row = 0; row < dimensions; ++row) {
				for (std::size_t column = 0; column < dimensions; ++column) {
					covariance(row, column) /= denominator;
				}
				trace += covariance(row, row);
			}
			const Number ridge = shrinkage * trace / static_cast<Number>(dimensions);
			if (!(ridge > Number(0))) {
				throw std::domain_error("LinearDiscriminant::fit: the covariance is degenerate");
			}
			for (std::size_t index = 0; index < dimensions; ++index) {
				covariance(index, index) += ridge;
			}

			const Matrix<Number> inverse = md::mathematic::inverse(covariance);
			priors.assign(classes, Number(0));
			weights.assign(classes, std::vector<Number>(dimensions, Number(0)));
			offsets.assign(classes, Number(0));
			for (std::size_t label = 0; label < classes; ++label) {
				priors[label] = static_cast<Number>(sizes[label]) / static_cast<Number>(count);
				Number quadratic = Number(0);
				for (std::size_t row = 0; row < dimensions; ++row) {
					Number value = Number(0);
					for (std::size_t column = 0; column < dimensions; ++column) {
						value += inverse(row, column) * means[label][column];
					}
					weights[label][row] = value;
					quadratic += means[label][row] * value;
				}
				offsets[label] = -quadratic / Number(2)
					+ static_cast<Number>(std::log(std::max(priors[label], Number(1e-12))));
			}
		}

		/// @brief  The class labels in ascending order.
		const std::vector<std::size_t>& classes() const {
			return labels;
		}

		/// @brief  The decision score of every class for one sample.
		/// @throws std::logic_error  When the model has not been fitted.
		/// @throws std::invalid_argument  When the feature has the wrong length.
		std::vector<Number> decision(const std::vector<Number>& feature) const {
			if (labels.empty()) {
				throw std::logic_error("LinearDiscriminant::decision: the model is not fitted");
			}
			if (feature.size() != featureCount) {
				throw std::invalid_argument("LinearDiscriminant::decision: the feature has the wrong length");
			}
			std::vector<Number> scores(labels.size(), Number(0));
			for (std::size_t label = 0; label < labels.size(); ++label) {
				Number value = offsets[label];
				for (std::size_t dimension = 0; dimension < featureCount; ++dimension) {
					value += feature[dimension] * weights[label][dimension];
				}
				scores[label] = value;
			}
			return scores;
		}

		/// @brief  The most likely class label of one sample.
		std::size_t predict(const std::vector<Number>& feature) const {
			const std::vector<Number> scores = decision(feature);
			const auto best = std::max_element(scores.begin(), scores.end());
			return labels[static_cast<std::size_t>(best - scores.begin())];
		}
	};

	/// @brief  Repeats a classifier over stratified folds.
	/// @param  features    One feature vector per sample.
	/// @param  targets     One class label per sample.
	/// @param  folds       Number of folds; at least two.
	/// @param  shrinkage   Passed to LinearDiscriminant::fit.
	/// @param  seed        Seed for the fold shuffle, so a report is reproducible.
	/// @return Accuracy, per-fold accuracy, confusion matrix and kappa.
	/// @throws std::invalid_argument  When the sizes disagree, a fold count is invalid
	///         or a class has fewer samples than folds.
	/// @note   Folds are stratified: every fold keeps the class proportions, which is
	///         what makes per-fold accuracy comparable across folds.
	template<typename Number = double>
	CrossValidationResult<Number> stratifiedCrossValidate(const std::vector<std::vector<Number>>& features,
		const std::vector<std::size_t>& targets, const std::size_t folds = 5,
		const Number& shrinkage = static_cast<Number>(1e-3), const std::uint32_t seed = 20261112u) {
		if (features.size() != targets.size()) {
			throw std::invalid_argument("stratifiedCrossValidate: the feature and label counts differ");
		}
		if (folds < 2) {
			throw std::invalid_argument("stratifiedCrossValidate: at least two folds are required");
		}
		if (features.size() < folds) {
			throw std::invalid_argument("stratifiedCrossValidate: fewer samples than folds");
		}

		std::vector<std::size_t> labels = targets;
		std::sort(labels.begin(), labels.end());
		labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
		if (labels.size() < 2) {
			throw std::invalid_argument("stratifiedCrossValidate: at least two classes are required");
		}

		// Stratified assignment: shuffle within each class, then deal round-robin.
		std::vector<std::size_t> assignment(targets.size(), 0);
		std::mt19937 generator(seed);
		for (const std::size_t label : labels) {
			std::vector<std::size_t> members;
			for (std::size_t index = 0; index < targets.size(); ++index) {
				if (targets[index] == label) {
					members.push_back(index);
				}
			}
			if (members.size() < folds) {
				throw std::invalid_argument("stratifiedCrossValidate: a class has fewer samples than folds");
			}
			for (std::size_t index = members.size(); index > 1; --index) {
				std::uniform_int_distribution<std::size_t> pick(0, index - 1);
				std::swap(members[index - 1], members[pick(generator)]);
			}
			for (std::size_t index = 0; index < members.size(); ++index) {
				assignment[members[index]] = index % folds;
			}
		}

		CrossValidationResult<Number> result;
		result.confusion = Matrix<Number>(labels.size(), labels.size(), Number(0));
		result.samples = features.size();

		std::size_t correct = 0;
		for (std::size_t fold = 0; fold < folds; ++fold) {
			std::vector<std::vector<Number>> trainingFeatures;
			std::vector<std::size_t> trainingTargets;
			std::vector<std::size_t> testIndices;
			for (std::size_t index = 0; index < features.size(); ++index) {
				if (assignment[index] == fold) {
					testIndices.push_back(index);
				}
				else {
					trainingFeatures.push_back(features[index]);
					trainingTargets.push_back(targets[index]);
				}
			}
			if (testIndices.empty()) {
				continue;
			}
			if (trainingTargets.size() < labels.size()) {
				throw std::invalid_argument("stratifiedCrossValidate: a training split lost a class");
			}

			LinearDiscriminant<Number> model;
			model.fit(trainingFeatures, trainingTargets, shrinkage);

			std::size_t foldCorrect = 0;
			for (const std::size_t index : testIndices) {
				const std::size_t predicted = model.predict(features[index]);
				const auto truePosition = std::lower_bound(labels.begin(), labels.end(), targets[index]);
				const auto predictedPosition = std::lower_bound(labels.begin(), labels.end(), predicted);
				result.confusion(static_cast<std::size_t>(truePosition - labels.begin()),
					static_cast<std::size_t>(predictedPosition - labels.begin())) += Number(1);
				if (predicted == targets[index]) {
					++foldCorrect;
				}
			}
			correct += foldCorrect;
			result.foldAccuracy.push_back(static_cast<Number>(foldCorrect)
				/ static_cast<Number>(testIndices.size()));
		}

		result.accuracy = static_cast<Number>(correct) / static_cast<Number>(features.size());

		// Cohen's kappa from the confusion matrix marginals.
		Number observed = Number(0);
		Number expected = Number(0);
		const Number total = static_cast<Number>(features.size());
		for (std::size_t row = 0; row < labels.size(); ++row) {
			Number rowTotal = Number(0);
			Number columnTotal = Number(0);
			for (std::size_t column = 0; column < labels.size(); ++column) {
				rowTotal += result.confusion(row, column);
				columnTotal += result.confusion(column, row);
			}
			observed += result.confusion(row, row);
			expected += rowTotal * columnTotal;
		}
		observed /= total;
		expected /= total * total;
		result.kappa = (expected < Number(1)) ? (observed - expected) / (Number(1) - expected) : Number(0);
		return result;
	}
}
