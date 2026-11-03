// ctgeom::calibration
//
// Voxel-size calibration from a reference sphere of known diameter,
// following the method of Yang et al., Chinese Physics C 38(4) 046202
// (2014), arXiv:1305.1354:
//
//   1. Scan a sphere of known true diameter W at several stage positions
//      D_i (displacement along the beam, measured by the stage).
//   2. In each projection, fit a circle to the sphere's silhouette edge to
//      get its diameter in detector pixels, W'_i.
//   3. The voxel size at that position is V_i = W / W'_i.
//      (Derivation: a feature of size W at source distance L_i projects to
//      W * SDD / L_i on the detector, i.e. W' = W * M_i / U pixels with
//      pixel pitch U, so W / W' = U / M_i = V.)
//   4. Because V_i = U * L_i / SDD and L_i = L_0 + D_i, voxel size is LINEAR
//      in stage position. A least-squares line V = a + b * D is the
//      calibration. Its slope should be U / SDD and its intercept
//      a / b = L_0 (source-to-axis distance at D = 0).
//
// Limits of this implementation, stated plainly:
//   * It works from silhouette edge POINTS (pixel coordinates). It does not
//     find the edge in a real image; that step needs real projections.
//   * The circle fit is the algebraic (Kasa) least-squares fit. It treats
//     the sphere's cone-beam silhouette as a circle, which is accurate for
//     a small sphere near the axis but is an approximation.
//   * It has only been exercised on synthetic data (see tests). No real
//     reference-object scan has been available.
#pragma once

#include "geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ctgeom {

struct Point2D {
    double x = 0.0;
    double y = 0.0;
};

struct CircleFit {
    double cx = 0.0;
    double cy = 0.0;
    double radius = 0.0;
    double rms_residual = 0.0;  // RMS of (distance to centre - radius), same units as the points
    double diameter() const { return 2.0 * radius; }
};

// Algebraic least-squares circle fit: minimise sum (x^2+y^2 + A x + B y + C)^2.
// Points are centred on their centroid first for numerical conditioning.
inline CircleFit fit_circle(const std::vector<Point2D>& pts) {
    const size_t n = pts.size();
    if (n < 3) throw std::invalid_argument("fit_circle: need at least 3 points");

    double mx = 0.0, my = 0.0;
    for (const auto& p : pts) { mx += p.x; my += p.y; }
    mx /= static_cast<double>(n);
    my /= static_cast<double>(n);

    // Normal equations for unknowns (A, B, C), rows [u v 1], rhs -(u^2+v^2).
    double suu = 0, suv = 0, svv = 0, su = 0, sv = 0;
    double r1 = 0, r2 = 0, r3 = 0;
    for (const auto& p : pts) {
        const double u = p.x - mx, v = p.y - my;
        const double q = u * u + v * v;
        suu += u * u; suv += u * v; svv += v * v; su += u; sv += v;
        r1 -= u * q; r2 -= v * q; r3 -= q;
    }
    double M[3][4] = {
        {suu, suv, su, r1},
        {suv, svv, sv, r2},
        {su,  sv,  static_cast<double>(n), r3}};

    // Gaussian elimination with partial pivoting.
    for (int col = 0; col < 3; ++col) {
        int piv = col;
        for (int r = col + 1; r < 3; ++r)
            if (std::fabs(M[r][col]) > std::fabs(M[piv][col])) piv = r;
        if (std::fabs(M[piv][col]) < 1e-12 * (1.0 + std::fabs(suu) + std::fabs(svv)))
            throw std::invalid_argument("fit_circle: points are degenerate (collinear or identical)");
        if (piv != col) for (int k = 0; k < 4; ++k) std::swap(M[piv][k], M[col][k]);
        for (int r = col + 1; r < 3; ++r) {
            const double f = M[r][col] / M[col][col];
            for (int k = col; k < 4; ++k) M[r][k] -= f * M[col][k];
        }
    }
    double sol[3];
    for (int r = 2; r >= 0; --r) {
        double s = M[r][3];
        for (int k = r + 1; k < 3; ++k) s -= M[r][k] * sol[k];
        sol[r] = s / M[r][r];
    }
    const double A = sol[0], B = sol[1], C = sol[2];
    const double ucx = -A / 2.0, ucy = -B / 2.0;
    const double r2v = ucx * ucx + ucy * ucy - C;
    if (r2v <= 0.0) throw std::invalid_argument("fit_circle: fit produced a non-positive radius");

    CircleFit f;
    f.cx = ucx + mx;
    f.cy = ucy + my;
    f.radius = std::sqrt(r2v);
    double ss = 0.0;
    for (const auto& p : pts) {
        const double d = std::hypot(p.x - f.cx, p.y - f.cy) - f.radius;
        ss += d * d;
    }
    f.rms_residual = std::sqrt(ss / static_cast<double>(n));
    return f;
}

// Voxel size implied by one projection: V = true diameter / pixel diameter.
inline double voxel_size_from_sphere(double true_diameter_mm, double diameter_pixels) {
    if (true_diameter_mm <= 0.0) throw std::invalid_argument("voxel_size_from_sphere: true diameter must be > 0");
    if (diameter_pixels <= 0.0) throw std::invalid_argument("voxel_size_from_sphere: pixel diameter must be > 0");
    return true_diameter_mm / diameter_pixels;
}

struct LineFit {
    double slope = 0.0;
    double intercept = 0.0;
    double rms_residual = 0.0;
    double slope_stderr = std::numeric_limits<double>::quiet_NaN();      // NaN when n == 2 (no spare data)
    double intercept_stderr = std::numeric_limits<double>::quiet_NaN();
    size_t n = 0;
};

inline LineFit fit_line(const std::vector<double>& x, const std::vector<double>& y) {
    if (x.size() != y.size()) throw std::invalid_argument("fit_line: x and y must have the same length");
    const size_t n = x.size();
    if (n < 2) throw std::invalid_argument("fit_line: need at least 2 points");
    double mx = 0, my = 0;
    for (size_t i = 0; i < n; ++i) { mx += x[i]; my += y[i]; }
    mx /= static_cast<double>(n);
    my /= static_cast<double>(n);
    double sxx = 0, sxy = 0;
    for (size_t i = 0; i < n; ++i) {
        sxx += (x[i] - mx) * (x[i] - mx);
        sxy += (x[i] - mx) * (y[i] - my);
    }
    if (sxx <= 0.0) throw std::invalid_argument("fit_line: all x values are identical");

    LineFit f;
    f.n = n;
    f.slope = sxy / sxx;
    f.intercept = my - f.slope * mx;
    double ss = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double r = y[i] - (f.intercept + f.slope * x[i]);
        ss += r * r;
    }
    f.rms_residual = std::sqrt(ss / static_cast<double>(n));
    if (n > 2) {
        const double s2 = ss / static_cast<double>(n - 2);
        f.slope_stderr = std::sqrt(s2 / sxx);
        f.intercept_stderr = std::sqrt(s2 * (1.0 / static_cast<double>(n) + mx * mx / sxx));
    }
    return f;
}

// Result of calibrating voxel size against stage position.
struct CalibrationResult {
    LineFit line;                 // V(D) = intercept + slope * D, V in mm, D in mm
    double implied_sdd = 0.0;     // U / slope
    double implied_sod_at_zero = 0.0;  // intercept / slope  (source-to-axis distance at D = 0)
    double detector_pixel = 0.0;  // U used (mm)

    double voxel_size_at(double stage_position_mm) const {
        return line.intercept + line.slope * stage_position_mm;
    }
};

// Calibrate from per-position sphere diameters (in detector pixels).
// stage_positions_mm: displacement D_i of the stage along the beam.
// diameters_px:       fitted silhouette diameter at each position.
inline CalibrationResult calibrate_from_diameters(const std::vector<double>& stage_positions_mm,
                                                   const std::vector<double>& diameters_px,
                                                   double true_diameter_mm,
                                                   double detector_pixel_mm) {
    if (detector_pixel_mm <= 0.0) throw std::invalid_argument("calibrate: detector pixel pitch must be > 0");
    if (stage_positions_mm.size() != diameters_px.size())
        throw std::invalid_argument("calibrate: positions and diameters must have the same length");
    if (stage_positions_mm.size() < 2)
        throw std::invalid_argument("calibrate: need at least 2 stage positions");

    std::vector<double> v;
    v.reserve(diameters_px.size());
    for (double d : diameters_px) v.push_back(voxel_size_from_sphere(true_diameter_mm, d));

    CalibrationResult r;
    r.line = fit_line(stage_positions_mm, v);
    if (r.line.slope <= 0.0)
        throw std::invalid_argument("calibrate: fitted slope is not positive; data inconsistent with the model "
                                    "(voxel size should grow as the stage moves away from the source)");
    r.detector_pixel = detector_pixel_mm;
    r.implied_sdd = detector_pixel_mm / r.line.slope;
    r.implied_sod_at_zero = r.line.intercept / r.line.slope;
    return r;
}

// Convenience: one projection's edge points per stage position.
inline CalibrationResult calibrate_from_edges(const std::vector<double>& stage_positions_mm,
                                               const std::vector<std::vector<Point2D>>& edge_points_per_position,
                                               double true_diameter_mm,
                                               double detector_pixel_mm) {
    if (stage_positions_mm.size() != edge_points_per_position.size())
        throw std::invalid_argument("calibrate: positions and edge-point sets must have the same length");
    std::vector<double> diam;
    for (const auto& pts : edge_points_per_position) diam.push_back(fit_circle(pts).diameter());
    return calibrate_from_diameters(stage_positions_mm, diam, true_diameter_mm, detector_pixel_mm);
}

// Compare a calibration against a nominal geometry (e.g. the one in .xtekct).
// Percent differences are relative to the nominal value.
struct CalibrationCheck {
    double sdd_pct_diff = 0.0;
    double sod_pct_diff = 0.0;
    double voxel_size_pct_diff = 0.0;   // calibrated V at D=0 vs nominal voxel_size_x
    bool flagged = false;
};

inline CalibrationCheck compare_calibration_to_geometry(const CalibrationResult& c, const Geometry& nominal,
                                                         double tolerance_pct = 0.5) {
    nominal.validate();
    auto pct = [](double got, double want) { return std::fabs(got - want) / std::fabs(want) * 100.0; };
    CalibrationCheck k;
    k.sdd_pct_diff = pct(c.implied_sdd, nominal.sdd);
    k.sod_pct_diff = pct(c.implied_sod_at_zero, nominal.sod);
    k.voxel_size_pct_diff = pct(c.voxel_size_at(0.0), voxel_size_x(nominal));
    k.flagged = k.sdd_pct_diff > tolerance_pct || k.sod_pct_diff > tolerance_pct ||
                k.voxel_size_pct_diff > tolerance_pct;
    return k;
}

// --- Synthetic data for testing the calibration code ---------------------
//
// Simulates the silhouette edge of a sphere of true diameter W at stage
// position D, for nominal geometry g (SOD is the source-to-axis distance at
// D = 0). Edge points lie on a circle of diameter W * SDD / (SOD + D) / U
// pixels, with optional Gaussian radial noise (pixels). Deterministic for a
// given seed. This tests the fitting code only; it says nothing about how
// well an edge can be found in a real image.
inline std::vector<Point2D> simulate_sphere_edge(const Geometry& g, double stage_position_mm,
                                                  double true_diameter_mm, size_t n_points,
                                                  double noise_sigma_px, unsigned seed,
                                                  double centre_x_px = 1000.0, double centre_y_px = 1000.0) {
    g.validate();
    const double L = g.sod + stage_position_mm;
    if (L <= 0.0) throw std::invalid_argument("simulate_sphere_edge: stage position puts the sphere behind the source");
    const double diameter_px = true_diameter_mm * (g.sdd / L) / g.det_pixel_x;
    const double radius_px = diameter_px / 2.0;
    const double pi = 3.14159265358979323846;

    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, noise_sigma_px > 0.0 ? noise_sigma_px : 1.0);
    std::vector<Point2D> pts;
    pts.reserve(n_points);
    for (size_t i = 0; i < n_points; ++i) {
        const double th = 2.0 * pi * static_cast<double>(i) / static_cast<double>(n_points);
        const double r = radius_px + (noise_sigma_px > 0.0 ? noise(rng) : 0.0);
        pts.push_back({centre_x_px + r * std::cos(th), centre_y_px + r * std::sin(th)});
    }
    return pts;
}

} // namespace ctgeom
