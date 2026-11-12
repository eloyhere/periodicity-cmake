/// @file   topography.h
/// @brief  A 10-20 electrode layout on the projected head disc, and the thin-plate
///         spline that turns per-channel values into a topographic map.
///
/// Method note: the interpolation runs on the projected two-dimensional disc, not
/// on the sphere. A true spherical spline (Perrin) needs three-dimensional
/// electrode coordinates and a Legendre series kernel; the planar thin-plate
/// spline reproduces linear fields exactly, interpolates the electrodes exactly,
/// and needs one small linear solve, which is the right trade for a display map.
/// Swapping in the spherical kernel later only changes this file.

#pragma once

#include <md/mathematic/kernel.h>
#include <md/mathematic/matrix.h>

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace md::mathematic {

	/// @brief  One electrode on the projected head disc.
	///
	/// Coordinates live on the unit disc: x grows to the subject's right, y grows
	/// towards the nasion, and the rim of the disc is the head outline.
	///
	/// @tparam Number  Floating-point type used for the coordinates.
	template<typename Number = double>
	struct TopographicPoint {
		std::string name;	///< Electrode label such as "Cz".
		Number x = Number(0);	///< Horizontal coordinate on the disc.
		Number y = Number(0);	///< Vertical coordinate on the disc.
	};

	/// @brief  The conventional 19 electrodes of the 10-20 system.
	/// @return The layout, in the usual reading order.
	/// @note   The coordinates are the standard top-down drawing, not a spherical
	///         projection of measured positions; they are meant for display.
	template<typename Number = double>
	std::vector<TopographicPoint<Number>> standard1020Layout() {
		return {
			{ "Fp1", static_cast<Number>(-0.30), static_cast<Number>(0.95) },
			{ "Fp2", static_cast<Number>(0.30), static_cast<Number>(0.95) },
			{ "F7", static_cast<Number>(-0.80), static_cast<Number>(0.60) },
			{ "F3", static_cast<Number>(-0.40), static_cast<Number>(0.55) },
			{ "Fz", static_cast<Number>(0.00), static_cast<Number>(0.55) },
			{ "F4", static_cast<Number>(0.40), static_cast<Number>(0.55) },
			{ "F8", static_cast<Number>(0.80), static_cast<Number>(0.60) },
			{ "T7", static_cast<Number>(-1.00), static_cast<Number>(0.00) },
			{ "C3", static_cast<Number>(-0.45), static_cast<Number>(0.00) },
			{ "Cz", static_cast<Number>(0.00), static_cast<Number>(0.00) },
			{ "C4", static_cast<Number>(0.45), static_cast<Number>(0.00) },
			{ "T8", static_cast<Number>(1.00), static_cast<Number>(0.00) },
			{ "P7", static_cast<Number>(-0.80), static_cast<Number>(-0.60) },
			{ "P3", static_cast<Number>(-0.40), static_cast<Number>(-0.55) },
			{ "Pz", static_cast<Number>(0.00), static_cast<Number>(-0.55) },
			{ "P4", static_cast<Number>(0.40), static_cast<Number>(-0.55) },
			{ "P8", static_cast<Number>(0.80), static_cast<Number>(-0.60) },
			{ "O1", static_cast<Number>(-0.30), static_cast<Number>(-0.95) },
			{ "O2", static_cast<Number>(0.30), static_cast<Number>(-0.95) }
		};
	}

	/// @brief  A thin-plate spline over the projected head disc.
	///
	/// The interpolant is a0 + a1 x + a2 y + sum of w_i r_i^2 log r_i, solved once
	/// per montage. It reproduces linear fields exactly and passes through every
	/// electrode; outside the electrode hull it extrapolates smoothly rather than
	/// clamping.
	///
	/// @tparam Number  Floating-point type used for coordinates and values.
	template<typename Number = double>
	class Topography {
	private:
		std::vector<TopographicPoint<Number>> points;	///< Electrode positions.
		std::vector<Number> weights;					///< Spline weights, one per electrode.
		Number constant = Number(0);					///< Affine part.
		Number slopeX = Number(0);						///< Affine part.
		Number slopeY = Number(0);						///< Affine part.

		/// @brief  The thin-plate kernel, delegated to kernel.h.
		/// @note   The previous local copy carried an extra factor of one half. A
		///         constant scale on the kernel only divides the fitted weights, so
		///         the interpolant is unchanged; the definition now lives in one place.
		static Number kernel(const Number& radiusSquared) {
			return thinPlateSplineKernel(static_cast<Number>(std::sqrt(radiusSquared)));
		}

	public:
		/// @brief  Fits a map to per-electrode values.
		/// @param  layout  Electrode positions, at least three and not all collinear.
		/// @param  values  One value per electrode.
		/// @throws std::invalid_argument  When the sizes disagree or are too small.
		/// @throws std::domain_error  When the electrode configuration is degenerate.
		Topography(const std::vector<TopographicPoint<Number>>& layout, const std::vector<Number>& values)
			: points(layout) {
			if (points.size() != values.size()) {
				throw std::invalid_argument("Topography: the layout and value counts differ");
			}
			if (points.size() < 3) {
				throw std::invalid_argument("Topography: at least three electrodes are required");
			}

			const std::size_t count = points.size();
			const std::size_t size = count + 3;
			Matrix<Number> system(size, size, Number(0));
			std::vector<Number> right(size, Number(0));

			for (std::size_t row = 0; row < count; ++row) {
				for (std::size_t column = 0; column < count; ++column) {
					const Number dx = points[row].x - points[column].x;
					const Number dy = points[row].y - points[column].y;
					system(row, column) = kernel(dx * dx + dy * dy);
				}
				system(row, count) = Number(1);
				system(row, count + 1) = points[row].x;
				system(row, count + 2) = points[row].y;

				system(count, row) = Number(1);
				system(count + 1, row) = points[row].x;
				system(count + 2, row) = points[row].y;

				right[row] = values[row];
			}

			const std::vector<Number> solution = solve(system, right);
			weights.assign(solution.begin(), solution.begin() + static_cast<std::ptrdiff_t>(count));
			constant = solution[count];
			slopeX = solution[count + 1];
			slopeY = solution[count + 2];
		}

		/// @brief  The fitted value at a point on the disc.
		/// @param  x  Horizontal coordinate.
		/// @param  y  Vertical coordinate.
		Number valueAt(const Number& x, const Number& y) const {
			Number total = constant + slopeX * x + slopeY * y;
			for (std::size_t index = 0; index < points.size(); ++index) {
				const Number dx = x - points[index].x;
				const Number dy = y - points[index].y;
				total += weights[index] * kernel(dx * dx + dy * dy);
			}
			return total;
		}

		/// @brief  The electrode positions the map was fitted to.
		const std::vector<TopographicPoint<Number>>& layout() const {
			return points;
		}

		/// @brief  Samples the map on a square grid covering the disc.
		/// @param  resolution  Number of samples per axis; must be at least two.
		/// @return resolution rows of resolution values, row zero at y = -1 and
		///         column zero at x = -1, so the result reads like an image.
		/// @throws std::invalid_argument  When the resolution is below two.
		std::vector<std::vector<Number>> grid(const std::size_t resolution) const {
			if (resolution < 2) {
				throw std::invalid_argument("Topography::grid: the resolution must be at least two");
			}
			std::vector<std::vector<Number>> result(resolution, std::vector<Number>(resolution, Number(0)));
			const Number step = Number(2) / static_cast<Number>(resolution - 1);
			for (std::size_t row = 0; row < resolution; ++row) {
				const Number y = Number(-1) + step * static_cast<Number>(row);
				for (std::size_t column = 0; column < resolution; ++column) {
					const Number x = Number(-1) + step * static_cast<Number>(column);
					result[row][column] = valueAt(x, y);
				}
			}
			return result;
		}

		/// @brief  Tests whether a point lies inside the head outline.
		/// @param  x       Horizontal coordinate.
		/// @param  y       Vertical coordinate.
		/// @param  radius  Radius of the outline, one by default.
		static bool insideHead(const Number& x, const Number& y, const Number& radius = Number(1)) {
			return x * x + y * y <= radius * radius;
		}
	};
}
