/// @file   decomposition.h
/// @brief  Matrix decompositions used by spatial filtering: symmetric and
///         generalized eigenproblems, the singular value decomposition and the
///         Moore-Penrose pseudo-inverse.

#pragma once

#include <md/mathematic/matrix.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace md::mathematic {

	/// @brief  Eigenvalues and eigenvectors of a real symmetric matrix.
	///
	/// @tparam Number  Floating-point type used for the entries.
	template<typename Number = double>
	struct EigenDecomposition {
		std::vector<Number> values;		///< Eigenvalues in ascending order.
		Matrix<Number> vectors;			///< Eigenvectors as columns, in the same order.
	};

	/// @brief  The eigen-decomposition of a real symmetric matrix by Jacobi rotations.
	/// @param  matrix     Symmetric matrix.
	/// @param  maxSweeps  Maximum number of sweeps; 64 is far more than enough.
	/// @return Eigenvalues ascending and orthonormal eigenvectors as columns.
	/// @throws std::invalid_argument  When the matrix is not square or not symmetric.
	/// @note   Jacobi is chosen over QR iteration because the matrices here are tiny
	///         (at most a few dozen channels) and it delivers orthonormal vectors
	///         without a separate back-transformation.
	template<typename Number = double>
	EigenDecomposition<Number> symmetricEigen(const Matrix<Number>& matrix,
		const unsigned maxSweeps = 64) {
		if (!matrix.isSquare()) {
			throw std::invalid_argument("symmetricEigen: the matrix must be square");
		}
		const std::size_t size = matrix.rows();
		EigenDecomposition<Number> result;
		result.values.assign(size, Number(0));
		result.vectors = Matrix<Number>::identity(size);
		if (size == 0) {
			return result;
		}

		Number scale = Number(0);
		for (const Number& value : matrix.data()) {
			scale = std::max(scale, std::abs(value));
		}
		const Number tolerance = std::numeric_limits<Number>::epsilon()
			* std::max(scale, Number(1)) * static_cast<Number>(size);

		Matrix<Number> working(matrix);
		for (std::size_t row = 0; row < size; ++row) {
			for (std::size_t column = row + 1; column < size; ++column) {
				if (std::abs(working(row, column) - working(column, row)) > tolerance) {
					throw std::invalid_argument("symmetricEigen: the matrix is not symmetric");
				}
				working(row, column) = working(column, row) = (working(row, column) + working(column, row))
					/ Number(2);
			}
		}

		for (unsigned sweep = 0; sweep < maxSweeps; ++sweep) {
			Number offDiagonal = Number(0);
			for (std::size_t row = 0; row < size; ++row) {
				for (std::size_t column = row + 1; column < size; ++column) {
					offDiagonal += working(row, column) * working(row, column);
				}
			}
			if (offDiagonal <= tolerance * tolerance) {
				break;
			}

			for (std::size_t p = 0; p + 1 < size; ++p) {
				for (std::size_t q = p + 1; q < size; ++q) {
					const Number entry = working(p, q);
					if (std::abs(entry) <= tolerance * tolerance) {
						continue;
					}

					const Number theta = (working(q, q) - working(p, p)) / (Number(2) * entry);
					const Number sign = theta >= Number(0) ? Number(1) : Number(-1);
					const Number tangent = sign / (std::abs(theta) + static_cast<Number>(std::sqrt(theta * theta + Number(1))));
					const Number cosine = Number(1) / static_cast<Number>(std::sqrt(tangent * tangent + Number(1)));
					const Number sine = tangent * cosine;

					for (std::size_t index = 0; index < size; ++index) {
						const Number left = working(index, p);
						const Number right = working(index, q);
						working(index, p) = cosine * left - sine * right;
						working(index, q) = sine * left + cosine * right;
					}
					for (std::size_t index = 0; index < size; ++index) {
						const Number left = working(p, index);
						const Number right = working(q, index);
						working(p, index) = cosine * left - sine * right;
						working(q, index) = sine * left + cosine * right;
					}
					for (std::size_t index = 0; index < size; ++index) {
						const Number left = result.vectors(index, p);
						const Number right = result.vectors(index, q);
						result.vectors(index, p) = cosine * left - sine * right;
						result.vectors(index, q) = sine * left + cosine * right;
					}
				}
			}
		}

		std::vector<std::size_t> order(size);
		std::iota(order.begin(), order.end(), std::size_t{ 0 });
		for (std::size_t index = 0; index < size; ++index) {
			result.values[index] = working(index, index);
		}
		std::sort(order.begin(), order.end(), [&result](const std::size_t left, const std::size_t right) {
			return result.values[left] < result.values[right];
		});

		Matrix<Number> sortedVectors(size, size, Number(0));
		std::vector<Number> sortedValues(size);
		for (std::size_t index = 0; index < size; ++index) {
			sortedValues[index] = result.values[order[index]];
			for (std::size_t row = 0; row < size; ++row) {
				sortedVectors(row, index) = result.vectors(row, order[index]);
			}
		}
		result.values = std::move(sortedValues);
		result.vectors = std::move(sortedVectors);
		return result;
	}

	/// @brief  The generalized eigen-decomposition A v = lambda B v.
	/// @param  matrix    Symmetric matrix A.
	/// @param  metric    Symmetric positive definite matrix B.
	/// @return Eigenvalues ascending and B-orthonormal eigenvectors as columns.
	/// @throws std::invalid_argument  When the shapes differ.
	/// @throws std::domain_error  When B is not positive definite.
	/// @note   This is the decomposition behind CSP: A is the class covariance and
	///         B the sum of the class covariances.
	template<typename Number = double>
	EigenDecomposition<Number> generalizedEigen(const Matrix<Number>& matrix, const Matrix<Number>& metric) {
		if (!matrix.isSquare() || matrix.rows() != metric.rows()) {
			throw std::invalid_argument("generalizedEigen: the matrices must be square and equally sized");
		}
		const std::size_t size = matrix.rows();
		const Matrix<Number> lower = cholesky(metric);

		// X = L^-1 A, by one forward substitution per column.
		Matrix<Number> intermediate(size, size, Number(0));
		std::vector<Number> column(size);
		for (std::size_t index = 0; index < size; ++index) {
			for (std::size_t row = 0; row < size; ++row) {
				column[row] = matrix(row, index);
			}
			for (std::size_t row = 0; row < size; ++row) {
				Number total = column[row];
				for (std::size_t inner = 0; inner < row; ++inner) {
					total -= lower(row, inner) * intermediate(inner, index);
				}
				intermediate(row, index) = total / lower(row, row);
			}
		}

		// C = L^-1 A L^-T = W^T, where W = L^-1 X^T.
		Matrix<Number> transposed(size, size, Number(0));
		for (std::size_t row = 0; row < size; ++row) {
			for (std::size_t index = 0; index < size; ++index) {
				transposed(row, index) = intermediate(index, row);
			}
		}
		Matrix<Number> symmetric(size, size, Number(0));
		for (std::size_t index = 0; index < size; ++index) {
			for (std::size_t row = 0; row < size; ++row) {
				column[row] = transposed(row, index);
			}
			for (std::size_t row = 0; row < size; ++row) {
				Number total = column[row];
				for (std::size_t inner = 0; inner < row; ++inner) {
					total -= lower(row, inner) * symmetric(inner, index);
				}
				symmetric(row, index) = total / lower(row, row);
			}
		}
		symmetric = symmetric.transposed();

		// L^-1 A L^-T is symmetric in exact arithmetic, but two triangular solves
		// leave rounding asymmetry that can exceed the eigensolver's tolerance and
		// make it reject a perfectly valid problem. Average the triangles.
		for (std::size_t row = 0; row < size; ++row) {
			for (std::size_t other = row + 1; other < size; ++other) {
				const Number average = (symmetric(row, other) + symmetric(other, row)) / Number(2);
				symmetric(row, other) = average;
				symmetric(other, row) = average;
			}
		}

		EigenDecomposition<Number> result = symmetricEigen(symmetric);

		// Back-transform the eigenvectors: v = L^-T y, an upper solve per column.
		Matrix<Number> vectors(size, size, Number(0));
		for (std::size_t index = 0; index < size; ++index) {
			for (std::size_t row = size; row-- > 0;) {
				Number total = result.vectors(row, index);
				for (std::size_t inner = row + 1; inner < size; ++inner) {
					total -= lower(inner, row) * vectors(inner, index);
				}
				vectors(row, index) = total / lower(row, row);
			}
		}
		result.vectors = std::move(vectors);
		return result;
	}

	/// @brief  A thin singular value decomposition.
	///
	/// @tparam Number  Floating-point type used for the entries.
	template<typename Number = double>
	struct SingularValueDecomposition {
		std::vector<Number> values;		///< Singular values in descending order.
		Matrix<Number> left;			///< Left singular vectors, one column per value.
		Matrix<Number> right;			///< Right singular vectors, one column per value.
	};

	/// @brief  The thin singular value decomposition of a rectangular matrix.
	/// @param  matrix  Matrix to decompose.
	/// @return U, sigma and V with A = U diag(sigma) V^T.
	/// @note   Computed from the eigen-decomposition of A^T A, which is adequate for
	///         the small matrices used here; columns of U that belong to a numerically
	///         zero singular value are zero rather than orthonormal.
	template<typename Number = double>
	SingularValueDecomposition<Number> svd(const Matrix<Number>& matrix) {
		SingularValueDecomposition<Number> result;
		const std::size_t rows = matrix.rows();
		const std::size_t columns = matrix.columns();
		if (rows == 0 || columns == 0) {
			return result;
		}

		const Matrix<Number> gram = matrix.transposed() * matrix;
		const EigenDecomposition<Number> decomposition = symmetricEigen(gram);

		Number largest = Number(0);
		for (const Number& value : decomposition.values) {
			largest = std::max(largest, std::abs(value));
		}
		const Number tolerance = std::numeric_limits<Number>::epsilon() * std::max(largest, Number(1))
			* static_cast<Number>(std::max(rows, columns));

		const std::size_t rankBound = std::min(rows, columns);
		result.values.resize(rankBound);
		result.left = Matrix<Number>(rows, rankBound, Number(0));
		result.right = Matrix<Number>(columns, rankBound, Number(0));

		// Descending order, taken from the ascending eigen-decomposition.
		for (std::size_t index = 0; index < rankBound; ++index) {
			const std::size_t source = columns - 1 - index;
			const Number eigenvalue = std::max(decomposition.values[source], Number(0));
			const Number singular = static_cast<Number>(std::sqrt(eigenvalue));
			result.values[index] = singular;
			for (std::size_t row = 0; row < columns; ++row) {
				result.right(row, index) = decomposition.vectors(row, source);
			}
			if (singular > tolerance) {
				for (std::size_t row = 0; row < rows; ++row) {
					Number total = Number(0);
					for (std::size_t inner = 0; inner < columns; ++inner) {
						total += matrix(row, inner) * result.right(inner, index);
					}
					result.left(row, index) = total / singular;
				}
			}
		}
		return result;
	}

	/// @brief  The singular values of a matrix, in descending order.
	template<typename Number = double>
	std::vector<Number> singularValues(const Matrix<Number>& matrix) {
		return svd(matrix).values;
	}

	/// @brief  The Moore-Penrose pseudo-inverse.
	/// @param  matrix     Matrix to invert.
	/// @param  tolerance  Relative threshold below which a singular value is treated as zero.
	/// @return A matrix with the transposed shape.
	template<typename Number = double>
	Matrix<Number> pseudoInverse(const Matrix<Number>& matrix,
		const Number& tolerance = std::numeric_limits<Number>::epsilon() * Number(1024)) {
		const SingularValueDecomposition<Number> decomposition = svd(matrix);
		const std::size_t rows = matrix.rows();
		const std::size_t columns = matrix.columns();

		Number largest = Number(0);
		for (const Number& value : decomposition.values) {
			largest = std::max(largest, value);
		}
		const Number threshold = tolerance * std::max(largest, Number(1));

		Matrix<Number> result(columns, rows, Number(0));
		for (std::size_t index = 0; index < decomposition.values.size(); ++index) {
			const Number singular = decomposition.values[index];
			if (!(singular > threshold)) {
				continue;
			}
			const Number weight = Number(1) / singular;
			for (std::size_t row = 0; row < columns; ++row) {
				const Number left = decomposition.right(row, index) * weight;
				for (std::size_t column = 0; column < rows; ++column) {
					result(row, column) += left * decomposition.left(column, index);
				}
			}
		}
		return result;
	}
}
