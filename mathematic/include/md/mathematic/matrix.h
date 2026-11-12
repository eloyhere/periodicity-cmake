/// @file   matrix.h
/// @brief  A small dense matrix, the linear solves EEG processing needs, and
///         the covariance helper that spatial filters are built from.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace md::mathematic {

	/// @brief  A dense, row-major matrix of real numbers.
	///
	/// The type is deliberately small: EEG works with at most a few dozen
	/// channels, so a plain nested loop beats any blocked implementation and the
	/// header stays dependency free. Element access is unchecked; use at() when
	/// an index may come from outside.
	///
	/// @tparam Number  Floating-point type used for the entries.
	template<typename Number = double>
	class Matrix {
	private:
		std::size_t rowCount = 0;			///< Number of rows.
		std::size_t columnCount = 0;		///< Number of columns.
		std::vector<Number> entries;		///< Row-major storage.

	public:
		/// @brief  Constructs an empty 0x0 matrix.
		Matrix() = default;

		/// @brief  Constructs a matrix filled with one value.
		/// @param  rows     Number of rows.
		/// @param  columns  Number of columns.
		/// @param  fill     Value of every entry; zero by default.
		Matrix(const std::size_t rows, const std::size_t columns, const Number& fill = Number(0))
			: rowCount(rows), columnCount(columns), entries(rows * columns, fill) {}

		/// @brief  Constructs a matrix from row-major values.
		/// @param  rows     Number of rows.
		/// @param  columns  Number of columns.
		/// @param  values   Row-major entries; its size must be rows*columns.
		/// @throws std::invalid_argument  When the size does not match.
		Matrix(const std::size_t rows, const std::size_t columns, std::vector<Number> values)
			: rowCount(rows), columnCount(columns), entries(std::move(values)) {
			if (entries.size() != rowCount * columnCount) {
				throw std::invalid_argument("Matrix: the value count does not match rows * columns");
			}
		}

		/// @brief  The identity matrix.
		/// @param  size  Number of rows and columns.
		static Matrix identity(const std::size_t size) {
			Matrix result(size, size, Number(0));
			for (std::size_t index = 0; index < size; ++index) {
				result(index, index) = Number(1);
			}
			return result;
		}

		/// @brief  A diagonal matrix.
		/// @param  diagonal  Entries on the main diagonal.
		static Matrix diagonal(const std::vector<Number>& diagonal) {
			Matrix result(diagonal.size(), diagonal.size(), Number(0));
			for (std::size_t index = 0; index < diagonal.size(); ++index) {
				result(index, index) = diagonal[index];
			}
			return result;
		}

		/// @brief  Number of rows.
		std::size_t rows() const {
			return rowCount;
		}

		/// @brief  Number of columns.
		std::size_t columns() const {
			return columnCount;
		}

		/// @brief  Reports whether the matrix has no entries.
		bool empty() const {
			return entries.empty();
		}

		/// @brief  Reports whether the matrix is square.
		bool isSquare() const {
			return rowCount == columnCount;
		}

		/// @brief  Unchecked element access.
		/// @param  row     Row index, which must be below rows().
		/// @param  column  Column index, which must be below columns().
		Number& operator()(const std::size_t row, const std::size_t column) {
			return entries[row * columnCount + column];
		}

		/// @brief  Unchecked read access.
		const Number& operator()(const std::size_t row, const std::size_t column) const {
			return entries[row * columnCount + column];
		}

		/// @brief  Checked element access.
		/// @throws std::out_of_range  When an index is outside the matrix.
		const Number& at(const std::size_t row, const std::size_t column) const {
			if (row >= rowCount || column >= columnCount) {
				throw std::out_of_range("Matrix::at: the index lies outside the matrix");
			}
			return entries[row * columnCount + column];
		}

		/// @brief  The row-major storage.
		const std::vector<Number>& data() const {
			return entries;
		}

		/// @brief  The row-major storage, writable.
		std::vector<Number>& data() {
			return entries;
		}

		/// @brief  The transpose.
		Matrix transposed() const {
			Matrix result(columnCount, rowCount, Number(0));
			for (std::size_t row = 0; row < rowCount; ++row) {
				for (std::size_t column = 0; column < columnCount; ++column) {
					result(column, row) = (*this)(row, column);
				}
			}
			return result;
		}

		/// @brief  The average of the matrix and its transpose.
		/// @return A symmetric matrix; a no-op for a symmetric input.
		Matrix symmetrized() const {
			Matrix result(*this);
			for (std::size_t row = 0; row < rowCount; ++row) {
				for (std::size_t column = row + 1; column < columnCount; ++column) {
					const Number average = (result(row, column) + result(column, row)) / Number(2);
					result(row, column) = average;
					result(column, row) = average;
				}
			}
			return result;
		}

		/// @brief  The trace, the sum of the main diagonal.
		/// @throws std::invalid_argument  When the matrix is not square.
		Number trace() const {
			if (!isSquare()) {
				throw std::invalid_argument("Matrix::trace: the matrix must be square");
			}
			Number total = Number(0);
			for (std::size_t index = 0; index < rowCount; ++index) {
				total += (*this)(index, index);
			}
			return total;
		}

		/// @brief  The Frobenius norm.
		Number norm() const {
			Number total = Number(0);
			for (const Number& value : entries) {
				total += value * value;
			}
			return static_cast<Number>(std::sqrt(total));
		}

		/// @brief  Adds another matrix of the same shape.
		/// @throws std::invalid_argument  When the shapes differ.
		Matrix& operator+=(const Matrix& other) {
			requireSameShape(other, "Matrix::operator+=");
			for (std::size_t index = 0; index < entries.size(); ++index) {
				entries[index] += other.entries[index];
			}
			return *this;
		}

		/// @brief  Subtracts another matrix of the same shape.
		/// @throws std::invalid_argument  When the shapes differ.
		Matrix& operator-=(const Matrix& other) {
			requireSameShape(other, "Matrix::operator-=");
			for (std::size_t index = 0; index < entries.size(); ++index) {
				entries[index] -= other.entries[index];
			}
			return *this;
		}

		/// @brief  Scales every entry.
		Matrix& operator*=(const Number& factor) {
			for (Number& value : entries) {
				value *= factor;
			}
			return *this;
		}

		/// @brief  Divides every entry.
		/// @warning Undefined when @p divisor is zero.
		Matrix& operator/=(const Number& divisor) {
			for (Number& value : entries) {
				value /= divisor;
			}
			return *this;
		}

		/// @brief  Returns the sum of two matrices.
		Matrix operator+(const Matrix& other) const {
			Matrix result(*this);
			result += other;
			return result;
		}

		/// @brief  Returns the difference of two matrices.
		Matrix operator-(const Matrix& other) const {
			Matrix result(*this);
			result -= other;
			return result;
		}

		/// @brief  Returns the matrix scaled by a factor.
		Matrix operator*(const Number& factor) const {
			Matrix result(*this);
			result *= factor;
			return result;
		}

		/// @brief  Returns the matrix divided by a divisor.
		Matrix operator/(const Number& divisor) const {
			Matrix result(*this);
			result /= divisor;
			return result;
		}

		/// @brief  Returns the matrix product.
		/// @throws std::invalid_argument  When the inner dimensions differ.
		Matrix operator*(const Matrix& other) const {
			if (columnCount != other.rowCount) {
				throw std::invalid_argument("Matrix::operator*: the inner dimensions differ");
			}
			Matrix result(rowCount, other.columnCount, Number(0));
			for (std::size_t row = 0; row < rowCount; ++row) {
				for (std::size_t inner = 0; inner < columnCount; ++inner) {
					const Number factor = (*this)(row, inner);
					if (factor == Number(0)) {
						continue;
					}
					for (std::size_t column = 0; column < other.columnCount; ++column) {
						result(row, column) += factor * other(inner, column);
					}
				}
			}
			return result;
		}

		/// @brief  Returns the matrix-vector product.
		/// @throws std::invalid_argument  When the sizes differ.
		std::vector<Number> operator*(const std::vector<Number>& vector) const {
			if (vector.size() != columnCount) {
				throw std::invalid_argument("Matrix::operator*: the vector size does not match the columns");
			}
			std::vector<Number> result(rowCount, Number(0));
			for (std::size_t row = 0; row < rowCount; ++row) {
				Number total = Number(0);
				for (std::size_t column = 0; column < columnCount; ++column) {
					total += (*this)(row, column) * vector[column];
				}
				result[row] = total;
			}
			return result;
		}

		/// @brief  Tests whether two matrices agree within a tolerance.
		bool isApproximately(const Matrix& other,
			const Number& tolerance = std::numeric_limits<Number>::epsilon() * Number(64)) const {
			if (rowCount != other.rowCount || columnCount != other.columnCount) {
				return false;
			}
			const Number scale = std::max(norm(), other.norm());
			for (std::size_t index = 0; index < entries.size(); ++index) {
				if (std::abs(entries[index] - other.entries[index]) > tolerance * std::max(scale, Number(1))) {
					return false;
				}
			}
			return true;
		}

	private:
		/// @brief  Throws unless another matrix has the same shape.
		void requireSameShape(const Matrix& other, const char* operation) const {
			if (rowCount != other.rowCount || columnCount != other.columnCount) {
				throw std::invalid_argument(operation);
			}
		}
	};

	/// @brief  The dot product of two vectors.
	/// @throws std::invalid_argument  When the sizes differ.
	template<typename Number = double>
	Number dot(const std::vector<Number>& left, const std::vector<Number>& right) {
		if (left.size() != right.size()) {
			throw std::invalid_argument("dot: the vector sizes differ");
		}
		Number total = Number(0);
		for (std::size_t index = 0; index < left.size(); ++index) {
			total += left[index] * right[index];
		}
		return total;
	}

	/// @brief  The Euclidean norm of a vector.
	template<typename Number = double>
	Number norm(const std::vector<Number>& vector) {
		Number total = Number(0);
		for (const Number& value : vector) {
			total += value * value;
		}
		return static_cast<Number>(std::sqrt(total));
	}

	/// @brief  Solves a square linear system by LU decomposition with partial pivoting.
	/// @param  matrix  Coefficient matrix; must be square and nonsingular.
	/// @param  right   Right-hand side.
	/// @return The solution vector.
	/// @throws std::invalid_argument  When the shapes differ or the matrix is not square.
	/// @throws std::domain_error  When the matrix is singular to working precision.
	template<typename Number = double>
	std::vector<Number> solve(const Matrix<Number>& matrix, const std::vector<Number>& right) {
		if (!matrix.isSquare()) {
			throw std::invalid_argument("solve: the coefficient matrix must be square");
		}
		const std::size_t size = matrix.rows();
		if (right.size() != size) {
			throw std::invalid_argument("solve: the right-hand side size does not match");
		}

		Matrix<Number> factors(matrix);
		std::vector<Number> values(right);

		Number scale = Number(0);
		for (const Number& value : factors.data()) {
			scale = std::max(scale, std::abs(value));
		}
		const Number threshold = std::numeric_limits<Number>::epsilon() * scale * Number(16);

		for (std::size_t column = 0; column < size; ++column) {
			std::size_t pivot = column;
			for (std::size_t row = column + 1; row < size; ++row) {
				if (std::abs(factors(row, column)) > std::abs(factors(pivot, column))) {
					pivot = row;
				}
			}
			if (std::abs(factors(pivot, column)) <= threshold) {
				throw std::domain_error("solve: the matrix is singular to working precision");
			}
			if (pivot != column) {
				for (std::size_t index = 0; index < size; ++index) {
					std::swap(factors(column, index), factors(pivot, index));
				}
				std::swap(values[column], values[pivot]);
			}

			for (std::size_t row = column + 1; row < size; ++row) {
				const Number factor = factors(row, column) / factors(column, column);
				factors(row, column) = factor;
				for (std::size_t index = column + 1; index < size; ++index) {
					factors(row, index) -= factor * factors(column, index);
				}
				values[row] -= factor * values[column];
			}
		}

		for (std::size_t index = size; index-- > 0;) {
			Number total = values[index];
			for (std::size_t column = index + 1; column < size; ++column) {
				total -= factors(index, column) * values[column];
			}
			values[index] = total / factors(index, index);
		}
		return values;
	}

	/// @brief  Inverts a square matrix by solving against the identity.
	/// @param  matrix  Matrix to invert.
	/// @return The inverse.
	/// @throws std::domain_error  When the matrix is singular to working precision.
	/// @note   Implemented as one solve() per identity column rather than a
	///         hand-rolled paired LU/forward/back substitution: the extra
	///         factorisations cost a factor of about three at these sizes and
	///         keep a single, already-tested elimination path.
	template<typename Number = double>
	Matrix<Number> inverse(const Matrix<Number>& matrix) {
		if (!matrix.isSquare()) {
			throw std::invalid_argument("inverse: the matrix must be square");
		}
		const std::size_t size = matrix.rows();
		Matrix<Number> result(size, size, Number(0));
		for (std::size_t column = 0; column < size; ++column) {
			std::vector<Number> unit(size, Number(0));
			unit[column] = Number(1);
			const std::vector<Number> solution = solve(matrix, unit);
			for (std::size_t row = 0; row < size; ++row) {
				result(row, column) = solution[row];
			}
		}
		return result;
	}

	/// @brief  The transpose of a matrix.
	template<typename Number = double>
	Matrix<Number> transpose(const Matrix<Number>& matrix) {
		Matrix<Number> result(matrix.columns(), matrix.rows(), Number(0));
		for (std::size_t row = 0; row < matrix.rows(); ++row) {
			for (std::size_t column = 0; column < matrix.columns(); ++column) {
				result(column, row) = matrix(row, column);
			}
		}
		return result;
	}

	/// @brief  The product of two matrices.
	/// @throws std::invalid_argument  When the inner dimensions disagree.
	template<typename Number = double>
	Matrix<Number> multiply(const Matrix<Number>& left, const Matrix<Number>& right) {
		if (left.columns() != right.rows()) {
			throw std::invalid_argument("multiply: the inner dimensions disagree");
		}
		Matrix<Number> result(left.rows(), right.columns(), Number(0));
		for (std::size_t row = 0; row < left.rows(); ++row) {
			for (std::size_t inner = 0; inner < left.columns(); ++inner) {
				const Number value = left(row, inner);
				if (value == Number(0)) {
					continue;
				}
				for (std::size_t column = 0; column < right.columns(); ++column) {
					result(row, column) += value * right(inner, column);
				}
			}
		}
		return result;
	}

	/// @brief  The lower-triangular Cholesky factor of a symmetric positive definite matrix.
	/// @param  matrix  Symmetric positive definite matrix.
	/// @return L such that L L^T equals @p matrix.
	/// @throws std::invalid_argument  When the matrix is not square or not symmetric.
	/// @throws std::domain_error  When the matrix is not positive definite.
	template<typename Number = double>
	Matrix<Number> cholesky(const Matrix<Number>& matrix) {
		if (!matrix.isSquare()) {
			throw std::invalid_argument("cholesky: the matrix must be square");
		}
		const std::size_t size = matrix.rows();
		Number scale = Number(0);
		for (const Number& value : matrix.data()) {
			scale = std::max(scale, std::abs(value));
		}
		const Number tolerance = std::numeric_limits<Number>::epsilon() * std::max(scale, Number(1)) * Number(64);

		Matrix<Number> lower(size, size, Number(0));
		for (std::size_t row = 0; row < size; ++row) {
			for (std::size_t column = 0; column <= row; ++column) {
				Number total = matrix(row, column);
				for (std::size_t inner = 0; inner < column; ++inner) {
					total -= lower(row, inner) * lower(column, inner);
				}
				if (row == column) {
					if (!(total > tolerance)) {
						throw std::domain_error("cholesky: the matrix is not positive definite");
					}
					lower(row, column) = static_cast<Number>(std::sqrt(total));
				}
				else {
					lower(row, column) = total / lower(column, column);
				}
			}
		}
		return lower;
	}

	/// @brief  The channel covariance matrix of multichannel recordings.
	/// @param  channels  One row of samples per channel; all rows must be equally long.
	/// @return An n-by-n matrix whose (i, j) entry is the mean of x_i x_j over time.
	/// @throws std::invalid_argument  When the rows differ in length.
	/// @note   The mean is not removed; callers that need a centred covariance should
	///         subtract the channel mean first, which keeps that decision explicit.
	template<typename Number = double>
	Matrix<Number> covariance(const std::vector<std::vector<Number>>& channels) {
		const std::size_t rows = channels.size();
		if (rows == 0) {
			return Matrix<Number>();
		}
		const std::size_t samples = channels.front().size();
		for (const auto& channel : channels) {
			if (channel.size() != samples) {
				throw std::invalid_argument("covariance: the channels differ in length");
			}
		}
		if (samples == 0) {
			return Matrix<Number>(rows, rows, Number(0));
		}

		Matrix<Number> result(rows, rows, Number(0));
		const Number divisor = static_cast<Number>(samples);
		for (std::size_t first = 0; first < rows; ++first) {
			for (std::size_t second = first; second < rows; ++second) {
				Number total = Number(0);
				for (std::size_t index = 0; index < samples; ++index) {
					total += channels[first][index] * channels[second][index];
				}
				total /= divisor;
				result(first, second) = total;
				result(second, first) = total;
			}
		}
		return result;
	}
}
