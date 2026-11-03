// Tests for calibration.hpp and the magnification-aware tolerance.
// All calibration data here is SYNTHETIC: these tests show the fitting
// code is correct, not that edge-finding on real projections works.
#include "ctgeom/geometry.hpp"
#include "ctgeom/calibration.hpp"
#include "ctgeom/compare.hpp"

#include <iostream>
#include <cmath>
#include <stdexcept>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::cerr << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; ++g_fail; } \
    else { ++g_pass; } \
} while (0)

static bool close(double a, double b, double rel_tol = 1e-9) {
    return std::fabs(a - b) <= rel_tol * std::max(std::fabs(a), std::fabs(b));
}

template <class F>
static bool throws_invalid(F f) {
    try { f(); } catch (const std::invalid_argument&) { return true; } catch (...) { return false; }
    return false;
}

// The real scan's geometry (poly_isro_mioni1_fbp).
static ctgeom::Geometry real_geom() { return ctgeom::Geometry{152.9016, 1123.142, 0.15, 0.15}; }

void test_circle_fit_exact() {
    std::vector<ctgeom::Point2D> pts;
    const double pi = 3.14159265358979323846;
    for (int i = 0; i < 50; ++i) {
        double th = 2 * pi * i / 50.0;
        pts.push_back({300.0 + 80.0 * std::cos(th), -40.0 + 80.0 * std::sin(th)});
    }
    auto f = ctgeom::fit_circle(pts);
    CHECK(close(f.cx, 300.0, 1e-9), "circle fit recovers centre x");
    CHECK(close(f.cy, -40.0, 1e-9), "circle fit recovers centre y");
    CHECK(close(f.radius, 80.0, 1e-9), "circle fit recovers radius");
    CHECK(f.rms_residual < 1e-9, "circle fit residual is ~0 on exact circle");
    CHECK(close(f.diameter(), 160.0, 1e-9), "diameter = 2 * radius");
}

void test_circle_fit_rejects_bad_input() {
    CHECK(throws_invalid([] { ctgeom::fit_circle({{0, 0}, {1, 1}}); }), "fewer than 3 points throws");
    CHECK(throws_invalid([] { ctgeom::fit_circle({{0, 0}, {1, 1}, {2, 2}, {3, 3}}); }), "collinear points throw");
    CHECK(throws_invalid([] { ctgeom::fit_circle({{5, 5}, {5, 5}, {5, 5}}); }), "identical points throw");
}

void test_line_fit() {
    auto f = ctgeom::fit_line({0, 1, 2, 3}, {1, 3, 5, 7});
    CHECK(close(f.slope, 2.0), "line fit slope");
    CHECK(close(f.intercept, 1.0), "line fit intercept");
    CHECK(f.rms_residual < 1e-12, "line fit residual ~0 on exact line");
    CHECK(throws_invalid([] { ctgeom::fit_line({1, 1, 1}, {1, 2, 3}); }), "identical x values throw");
    CHECK(throws_invalid([] { ctgeom::fit_line({1}, {1}); }), "single point throws");
    CHECK(throws_invalid([] { ctgeom::fit_line({1, 2}, {1}); }), "length mismatch throws");
    auto two = ctgeom::fit_line({0, 1}, {0, 1});
    CHECK(std::isnan(two.slope_stderr), "two points: no standard error available (NaN)");
}

void test_voxel_size_from_sphere() {
    CHECK(close(ctgeom::voxel_size_from_sphere(5.0, 250.0), 0.02), "V = true diameter / pixel diameter");
    CHECK(throws_invalid([] { ctgeom::voxel_size_from_sphere(0.0, 100.0); }), "zero true diameter throws");
    CHECK(throws_invalid([] { ctgeom::voxel_size_from_sphere(5.0, -1.0); }), "negative pixel diameter throws");
}

// Noise-free synthetic phantom with the real scan's geometry: the
// calibration must recover SDD, SOD and voxel size essentially exactly.
void test_calibration_recovers_geometry_noise_free() {
    auto g = real_geom();
    std::vector<double> pos = {-20, -10, 0, 10, 20};
    std::vector<std::vector<ctgeom::Point2D>> edges;
    for (double d : pos) edges.push_back(ctgeom::simulate_sphere_edge(g, d, 5.0, 360, 0.0, 1));

    auto c = ctgeom::calibrate_from_edges(pos, edges, 5.0, g.det_pixel_x);
    CHECK(close(c.implied_sdd, g.sdd, 1e-6), "noise-free: implied SDD matches truth");
    CHECK(close(c.implied_sod_at_zero, g.sod, 1e-6), "noise-free: implied SOD at D=0 matches truth");
    CHECK(close(c.voxel_size_at(0.0), ctgeom::voxel_size_x(g), 1e-6), "noise-free: V at D=0 matches 0.0204206 mm");
    CHECK(close(c.line.slope, g.det_pixel_x / g.sdd, 1e-6), "noise-free: slope = U / SDD");

    auto chk = ctgeom::compare_calibration_to_geometry(c, g);
    CHECK(!chk.flagged, "noise-free: calibration agrees with nominal geometry");
}

// Noisy edges (0.1 px radial sigma): should still recover geometry well.
void test_calibration_recovers_geometry_with_noise() {
    auto g = real_geom();
    std::vector<double> pos = {-20, -13, -7, 0, 7, 13, 20};
    std::vector<std::vector<ctgeom::Point2D>> edges;
    unsigned seed = 42;
    for (double d : pos) edges.push_back(ctgeom::simulate_sphere_edge(g, d, 5.0, 360, 0.1, seed++));

    auto c = ctgeom::calibrate_from_edges(pos, edges, 5.0, g.det_pixel_x);
    CHECK(close(c.implied_sdd, g.sdd, 5e-3), "noisy: implied SDD within 0.5% of truth");
    CHECK(close(c.implied_sod_at_zero, g.sod, 5e-3), "noisy: implied SOD within 0.5% of truth");
    CHECK(close(c.voxel_size_at(0.0), ctgeom::voxel_size_x(g), 5e-3), "noisy: V at D=0 within 0.5% of truth");
    CHECK(c.line.rms_residual > 0.0, "noisy: fit has nonzero residual");
    CHECK(!std::isnan(c.line.slope_stderr) && c.line.slope_stderr > 0.0, "noisy: slope standard error is reported");
}

// If the nominal geometry is wrong (SOD off by 5%), the calibration must say so.
void test_calibration_detects_wrong_nominal_geometry() {
    auto truth = real_geom();
    std::vector<double> pos = {-20, -10, 0, 10, 20};
    std::vector<std::vector<ctgeom::Point2D>> edges;
    for (double d : pos) edges.push_back(ctgeom::simulate_sphere_edge(truth, d, 5.0, 360, 0.0, 1));
    auto c = ctgeom::calibrate_from_edges(pos, edges, 5.0, truth.det_pixel_x);

    ctgeom::Geometry wrong = truth;
    wrong.sod *= 1.05;
    auto chk = ctgeom::compare_calibration_to_geometry(c, wrong);
    CHECK(chk.flagged, "5% wrong nominal SOD is flagged by calibration");
    CHECK(chk.sod_pct_diff > 4.0 && chk.sod_pct_diff < 5.5, "SOD discrepancy is about 5%");
}

void test_calibration_rejects_bad_input() {
    CHECK(throws_invalid([] { ctgeom::calibrate_from_diameters({0.0}, {250.0}, 5.0, 0.15); }),
          "a single stage position throws");
    CHECK(throws_invalid([] { ctgeom::calibrate_from_diameters({0.0, 1.0}, {250.0}, 5.0, 0.15); }),
          "length mismatch throws");
    CHECK(throws_invalid([] { ctgeom::calibrate_from_diameters({0.0, 1.0}, {250.0, 250.0}, 5.0, 0.15); }),
          "zero slope (no magnification change) throws");
    // Diameter shrinking as the stage moves AWAY from the source is the physically correct direction (V grows);
    // the reverse (V shrinking with D) contradicts the model and must throw.
    CHECK(throws_invalid([] { ctgeom::calibrate_from_diameters({0.0, 10.0}, {200.0, 250.0}, 5.0, 0.15); }),
          "negative slope throws");
    CHECK(throws_invalid([] { ctgeom::calibrate_from_diameters({0.0, 10.0}, {250.0, 200.0}, 5.0, 0.0); }),
          "zero detector pixel throws");
    ctgeom::Geometry g = real_geom();
    CHECK(throws_invalid([&] { ctgeom::simulate_sphere_edge(g, -1000.0, 5.0, 10, 0.0, 1); }),
          "simulated sphere behind the source throws");
}

void test_scaled_tolerance() {
    auto g = real_geom();
    CHECK(close(ctgeom::scaled_tolerance_pct(g), 0.5), "real scan (7.3x): scaled tolerance stays at the 0.5% floor");

    // High-magnification geometry: SDD 1123.14, M = 300 -> SOD = 3.7438 mm.
    ctgeom::Geometry hi{1123.14 / 300.0, 1123.14, 0.15, 0.15};
    double tol = ctgeom::scaled_tolerance_pct(hi);
    CHECK(tol > 3.5 && tol < 4.5, "300x: scaled tolerance is ~4% (3 sigma of 0.05 mm on SOD 3.74 mm)");
    CHECK(tol > ctgeom::scaled_tolerance_pct(g), "tolerance grows with magnification");
    CHECK(close(ctgeom::scaled_tolerance_pct(g, 0.0), 0.5), "zero positioning sigma falls back to the floor");
    CHECK(throws_invalid([&] { ctgeom::scaled_tolerance_pct(g, -1.0); }), "negative sigma throws");
}

int main() {
    test_circle_fit_exact();
    test_circle_fit_rejects_bad_input();
    test_line_fit();
    test_voxel_size_from_sphere();
    test_calibration_recovers_geometry_noise_free();
    test_calibration_recovers_geometry_with_noise();
    test_calibration_detects_wrong_nominal_geometry();
    test_calibration_rejects_bad_input();
    test_scaled_tolerance();

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
