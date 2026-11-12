/// @file   riemannian.h
/// @brief  The geometry of symmetric positive definite matrices: the affine
///         invariant metric, the Karcher mean, tangent-space vectors and the
///         minimum-distance-to-mean classifier.
///

#pragma once

#include <md/mathematic/decomposition.h>
#include <md/mathematic/matrix.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace md::mathematic {

	/// @brief  Applies a scalar function to the eigenvalues of a symmetric matrix.
	/// @param  matrix    Symmetric matrix.
	/// @param  operation Callable mapping one eigenvalue to its replacement.
	/// @return V f(Lambda) V^T.
	template<typename Number = double, typename Operation>
	Matrix<Number> spectralMap(const Matrix<Number>& matrix, Operation operation) {
		const EigenDecomposition<Number> spectrum = symmetricEigen(matrix);
		const std::size_t size = matrix.rows();
		Matrix<Number> result(size, size, Number(0));
		for (std::size_t row = 0; row < size; ++row) {
			for (std::size_t column = row; column < size; ++column) {
				Number total = Number(0);
				for (std::size_t index = 0; index < size; ++index) {
					total += spectrum.vectors(row, index) * operation(spectrum.values[index])
						* spectrum.vectors(column, index);
				}
				result(row, column) = total;
				result(column, row) = total;
			}
		}
		return result;
	}

	/// @brief  Throws when a matrix is not symmetric positive definite.
	/// @param  matrix  Matrix to check.
	/// @param  where   Name used in the exception message.
	template<typename Number = double>
	void requirePositiveDefinite(const Matrix<Number>& matrix, const char* where) {
		if (!matrix.isSquare()) {
			throw std::invalid_argument(std::string(where) + ": the matrix must be square");
		}
		const EigenDecomposition<Number> spectrum = symmetricEigen(matrix);
		if (!(spectrum.values.front() > Number(0))) {
			throw std::domain_error(std::string(where) + ": the matrix is not positive definite");
		}
	}

	/// @brief  The symmetric square root of a positive definite matrix.
	template<typename Number = double>
	Matrix<Number> matrixSqrt(const Matrix<Number>& matrix) {
		requirePositiveDefinite(matrix, "matrixSqrt");
		return spectralMap(matrix, [](const Number& value) {
			return static_cast<Number>(std::sqrt(value));
		});
	}

	/// @brief  The inverse symmetric square root, the whitening matrix.
	template<typename Number = double>
	Matrix<Number> matrixInverseSqrt(const Matrix<Number>& matrix) {
		requirePositiveDefinite(matrix, "matrixInverseSqrt");
		return spectralMap(matrix, [](const Number& value) {
			return static_cast<Number>(1) / static_cast<Number>(std::sqrt(value));
		});
	}

	/// @brief  The matrix logarithm of a positive definite matrix.
	template<typename Number = double>
	Matrix<Number> matrixLog(const Matrix<Number>& matrix) {
		requirePositiveDefinite(matrix, "matrixLog");
		return spectralMap(matrix, [](const Number& value) {
			return static_cast<Number>(std::log(value));
		});
	}

	/// @brief  The matrix exponential of a symmetric matrix.
	template<typename Number = double>
	Matrix<Number> matrixExp(const Matrix<Number>& matrix) {
		return spectralMap(matrix, [](const Number& value) {
			return static_cast<Number>(std::exp(value));
		});
	}

	/// @brief  The affine invariant Riemannian distance between two SPD matrices.
	/// @return sqrt of the summed squared logarithms of the generalised eigenvalues,
	///         which is the length of the geodesic that joins the two matrices.
	/// @throws std::domain_error  When either argument is not positive definite.
	template<typename Number = double>
	Number riemannianDistance(const Matrix<Number>& first, const Matrix<Number>& second) {
		if (first.rows() != second.rows()) {
			throw std::invalid_argument("riemannianDistance: the matrices differ in size");
		}
		const Matrix<Number> whitening = matrixInverseSqrt(first);
		const Matrix<Number> normalised = multiply(multiply(whitening, second), whitening);
		const EigenDecomposition<Number> spectrum = symmetricEigen(normalised);
		Number total = Number(0);
		for (const Number& value : spectrum.values) {
			const Number logarithm = static_cast<Number>(std::log(std::max(value, Number(1e-300))));
			total += logarithm * logarithm;
		}
		return static_cast<Number>(std::sqrt(total));
	}

	/// @brief  Adds a ridge proportional to the mean diagonal.
	/// @param  matrix     SPD matrix, typically a sample covariance.
	/// @param  shrinkage  Fraction of the mean diagonal to add; 1e-3 is a safe default.
	/// @return The regularised matrix.
	template<typename Number = double>
	Matrix<Number> regularizeCovariance(const Matrix<Number>& matrix,
		const Number& shrinkage = static_cast<Number>(1e-3)) {
		if (!matrix.isSquare()) {
			throw std::invalid_argument("regularizeCovariance: the matrix must be square");
		}
		const std::size_t size = matrix.rows();
		Number trace = Number(0);
		for (std::size_t index = 0; index < size; ++index) {
			trace += matrix(index, index);
		}
		const Number ridge = shrinkage * trace / static_cast<Number>(size);
		Matrix<Number> result(matrix);
		for (std::size_t index = 0; index < size; ++index) {
			result(index, index) += (ridge > Number(0)) ? ridge : Number(1e-9);
		}
		return result;
	}

	/// @brief  The intrinsic (Karcher) mean of a set of SPD matrices.
	/// @param  matrices    SPD matrices, all of the same size.
	/// @param  iterations  Maximum number of fixed-point steps.
	/// @param  tolerance   Stop when the step length falls below this.
	/// @return The mean, the point that minimises the summed squared distances.
	/// @throws std::invalid_argument  When the set is empty or ragged.
	template<typename Number = double>
	Matrix<Number> riemannianMean(const std::vector<Matrix<Number>>& matrices,
		const unsigned iterations = 50, const Number& tolerance = static_cast<Number>(1e-8)) {
		if (matrices.empty()) {
			throw std::invalid_argument("riemannianMean: the set is empty");
		}
		const std::size_t size = matrices.front().rows();
		for (const auto& matrix : matrices) {
			if (matrix.rows() != size || matrix.columns() != size) {
				throw std::invalid_argument("riemannianMean: the matrices differ in size");
			}
		}

		// Arithmetic mean as the starting point; it is already close for similar inputs.
		Matrix<Number> current(size, size, Number(0));
		for (const auto& matrix : matrices) {
			for (std::size_t row = 0; row < size; ++row) {
				for (std::size_t column = 0; column < size; ++column) {
					current(row, column) += matrix(row, column);
				}
			}
		}
		const Number count = static_cast<Number>(matrices.size());
		for (std::size_t row = 0; row < size; ++row) {
			for (std::size_t column = 0; column < size; ++column) {
				current(row, column) /= count;
			}
		}
		requirePositiveDefinite(current, "riemannianMean");

		for (unsigned iteration = 0; iteration < iterations; ++iteration) {
			const Matrix<Number> whitening = matrixInverseSqrt(current);
			Matrix<Number> average(size, size, Number(0));
			for (const auto& matrix : matrices) {
				const Matrix<Number> normalised = multiply(multiply(whitening, matrix), whitening);
				const Matrix<Number> logarithm = matrixLog(normalised);
				for (std::size_t row = 0; row < size; ++row) {
					for (std::size_t column = 0; column < size; ++column) {
						average(row, column) += logarithm(row, column) / count;
					}
				}
			}
			const Matrix<Number> step = matrixExp(average);
			const Matrix<Number> root = matrixSqrt(current);
			current = multiply(multiply(root, step), root);

			Number norm = Number(0);
			for (std::size_t row = 0; row < size; ++row) {
				for (std::size_t column = 0; column < size; ++column) {
					norm += average(row, column) * average(row, column);
				}
			}
			if (static_cast<Number>(std::sqrt(norm)) < tolerance) {
				break;
			}
		}
		return current;
	}

	/// @brief  Maps a covariance to the tangent space at a reference point.
	/// @param  covariance  SPD matrix to map.
	/// @param  reference   SPD origin of the tangent space, usually the class mean.
	/// @return The upper triangle of the logarithm, with off-diagonal entries scaled
	///         by sqrt(2) so the Euclidean norm of the vector equals the Riemannian
	///         distance to the reference.
	/// @throws std::domain_error  When either matrix is not positive definite.
	template<typename Number = double>
	std::vector<Number> tangentVector(const Matrix<Number>& covariance, const Matrix<Number>& reference) {
		const Matrix<Number> whitening = matrixInverseSqrt(reference);
		const Matrix<Number> normalised = multiply(multiply(whitening, covariance), whitening);
		const Matrix<Number> logarithm = matrixLog(normalised);
		const std::size_t size = covariance.rows();
		const Number scale = static_cast<Number>(std::sqrt(Number(2)));

		std::vector<Number> result;
		result.reserve(size * (size + 1) / 2);
		for (std::size_t row = 0; row < size; ++row) {
			for (std::size_t column = row; column < size; ++column) {
				result.push_back((row == column) ? logarithm(row, column) : logarithm(row, column) * scale);
			}
		}
		return result;
	}

	/// @brief  Minimum distance to mean: a classifier that compares covariances.
	///
	/// Each class is represented by its Karcher mean, and a trial is assigned to
	/// the class whose mean is closest in the affine invariant metric. No tangent
	/// space is involved, so the decision does not depend on the reference point.
	///
	/// @tparam Number  Floating-point type used for the entries.
	template<typename Number = double>
	class RiemannianMDM {
	private:
		std::vector<std::size_t> labels;		///< Class labels, ascending.
		std::vector<Matrix<Number>> means;		///< One Karcher mean per class.

	public:
		/// @brief  Fits one mean per class.
		/// @param  covariances  One SPD covariance per trial.
		/// @param  targets      One class label per trial.
		/// @throws std::invalid_argument  When the sizes disagree or a trial is empty.
		/// @throws std::domain_error  When a trial is not positive definite.
		void fit(const std::vector<Matrix<Number>>& covariances, const std::vector<std::size_t>& targets) {
			if (covariances.size() != targets.size()) {
				throw std::invalid_argument("RiemannianMDM::fit: the matrix and label counts differ");
			}
			if (covariances.empty()) {
				throw std::invalid_argument("RiemannianMDM::fit: the training set is empty");
			}

			labels = targets;
			std::sort(labels.begin(), labels.end());
			labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
			if (labels.size() < 2) {
				throw std::invalid_argument("RiemannianMDM::fit: at least two classes are required");
			}

			means.assign(labels.size(), Matrix<Number>());
			for (std::size_t label = 0; label < labels.size(); ++label) {
				std::vector<Matrix<Number>> group;
				for (std::size_t index = 0; index < covariances.size(); ++index) {
					if (targets[index] == labels[label]) {
						group.push_back(regularizeCovariance(covariances[index]));
					}
				}
				if (group.empty()) {
					throw std::invalid_argument("RiemannianMDM::fit: a class has no trials");
				}
				means[label] = riemannianMean(group);
			}
		}

		/// @brief  The class means in label order.
		const std::vector<Matrix<Number>>& classMeans() const {
			return means;
		}

		/// @brief  The distance from a trial to every class mean.
		/// @throws std::logic_error  When the model has not been fitted.
		std::vector<Number> distances(const Matrix<Number>& covariance) const {
			if (means.empty()) {
				throw std::logic_error("RiemannianMDM::distances: the model is not fitted");
			}
			const Matrix<Number> regularised = regularizeCovariance(covariance);
			std::vector<Number> result(means.size(), Number(0));
			for (std::size_t label = 0; label < means.size(); ++label) {
				result[label] = riemannianDistance(regularised, means[label]);
			}
			return result;
		}

		/// @brief  The label of the closest class mean.
		std::size_t predict(const Matrix<Number>& covariance) const {
			const std::vector<Number> values = distances(covariance);
			const auto best = std::min_element(values.begin(), values.end());
			return labels[static_cast<std::size_t>(best - values.begin())];
		}
	};
}
