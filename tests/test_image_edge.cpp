// Tests for image_edge.hpp and export.hpp. All images are SYNTHETIC.
#include "ctgeom/geometry.hpp"
#include "ctgeom/calibration.hpp"
#include "ctgeom/image_edge.hpp"
#include "ctgeom/export.hpp"

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

static ctgeom::Geometry real_geom() { return ctgeom::Geometry{152.9016, 1123.142, 0.15, 0.15}; }

void test_sample_bilinear() {
    ctgeom::Image im(3, 3, 0.0f);
    im.at(1, 1) = 4.0f;
    CHECK(close(im.sample(1.0, 1.0), 4.0), "bilinear sample at a pixel centre returns the pixel");
    CHECK(close(im.sample(1.5, 1.0), 2.0), "bilinear sample halfway between pixels averages them");
    bool threw = false;
    try { im.sample(-0.1, 1.0); } catch (const std::out_of_range&) { threw = true; }
    CHECK(threw, "sampling outside the image throws");
}

void test_edge_noise_free_dark_sphere() {
    // Dark sphere (low intensity) on bright background, centre 0.3/-1.3 off the pixel grid.
    auto img = ctgeom::render_disc_phantom(700, 700, 350.3, 348.7, 122.4, 8000.f, 10000.f, 0.8, 0.0, 1);
    auto pts = ctgeom::find_sphere_edge_points(img, 365.0, 335.0, 300.0);  // initial guess ~20 px off
    auto f = ctgeom::fit_circle(pts);
    CHECK(std::fabs(f.diameter() - 244.8) < 0.05, "noise-free: diameter within 0.05 px");
    CHECK(std::fabs(f.cx - 350.3) < 0.05 && std::fabs(f.cy - 348.7) < 0.05, "noise-free: centre within 0.05 px");
}

void test_edge_polarity_agnostic() {
    auto img = ctgeom::render_disc_phantom(700, 700, 350.0, 350.0, 100.0, 10000.f, 8000.f, 0.8, 0.0, 1);
    auto pts = ctgeom::find_sphere_edge_points(img, 350.0, 350.0, 300.0);
    CHECK(std::fabs(ctgeom::fit_circle(pts).diameter() - 200.0) < 0.05, "bright sphere on dark background also works");
}

void test_edge_with_noise() {
    // Contrast 2000, pixel noise sigma 60 (3% of contrast).
    auto img = ctgeom::render_disc_phantom(700, 700, 350.3, 348.7, 122.4, 8000.f, 10000.f, 0.8, 60.0, 5);
    auto pts = ctgeom::find_sphere_edge_points(img, 350.0, 350.0, 300.0);
    auto f = ctgeom::fit_circle(pts);
    CHECK(std::fabs(f.diameter() - 244.8) < 0.3, "noisy: diameter within 0.3 px");
}

void test_edge_rejects_bad_images() {
    ctgeom::Image flat(700, 700, 5000.0f);  // no sphere at all
    CHECK(throws_invalid([&] { ctgeom::find_sphere_edge_points(flat, 350, 350, 300); }), "flat image (no sphere) throws");

    auto img = ctgeom::render_disc_phantom(700, 700, 350.0, 350.0, 100.0, 8000.f, 10000.f, 0.8, 0.0, 1);
    CHECK(throws_invalid([&] { ctgeom::find_sphere_edge_points(img, 350, 350, 3.0); }), "max radius too small throws");
    // Edge at 100 px but rays only reach 110 px -> edge beyond the 85% window -> no clear edge.
    CHECK(throws_invalid([&] { ctgeom::find_sphere_edge_points(img, 350, 350, 110.0); }),
          "edge outside the 20%-85% window throws");
    bool threw = false;
    try { ctgeom::find_sphere_edge_points(img, 50.0, 350.0, 300.0); } catch (const std::out_of_range&) { threw = true; }
    CHECK(threw, "rays leaving the image throw out_of_range");
}

// End to end: synthetic images at several stage positions -> edge finder ->
// circle fit -> calibration -> compare to nominal geometry.
void test_end_to_end_calibration_from_images() {
    auto g = real_geom();
    std::vector<double> pos = {-20, -10, 0, 10, 20};
    std::vector<std::vector<ctgeom::Point2D>> edges;
    unsigned seed = 11;
    for (double d : pos) {
        const double diam_px = 5.0 * (g.sdd / (g.sod + d)) / g.det_pixel_x;
        auto img = ctgeom::render_disc_phantom(700, 700, 350.0, 350.0, diam_px / 2.0, 8000.f, 10000.f, 0.8, 40.0, seed++);
        edges.push_back(ctgeom::find_sphere_edge_points(img, 355.0, 345.0, 300.0));
    }
    auto c = ctgeom::calibrate_from_edges(pos, edges, 5.0, g.det_pixel_x);
    CHECK(close(c.implied_sdd, g.sdd, 5e-3), "images: implied SDD within 0.5% of truth");
    CHECK(close(c.implied_sod_at_zero, g.sod, 5e-3), "images: implied SOD within 0.5% of truth");
    CHECK(close(c.voxel_size_at(0.0), ctgeom::voxel_size_x(g), 5e-3), "images: voxel size at D=0 within 0.5% of truth");
    CHECK(!ctgeom::compare_calibration_to_geometry(c, g).flagged, "images: calibration agrees with nominal geometry");
}

void test_export_astra_and_tigre() {
    auto g = real_geom();
    const double v = ctgeom::voxel_size_x(g);
    auto a = ctgeom::to_astra_cone(g, v, 2850, 2850);
    CHECK(close(a.source_origin, 7487.6133, 1e-6), "ASTRA source_origin = SOD / V (real scan)");
    CHECK(close(a.origin_det, 47512.8119, 1e-6), "ASTRA origin_det = (SDD - SOD) / V (real scan)");
    CHECK(close(a.det_spacing_x, ctgeom::magnification(g), 1e-9), "ASTRA detector spacing in voxel units equals magnification");
    CHECK(a.det_row_count == 2850 && a.det_col_count == 2850, "ASTRA detector counts pass through");

    auto t = ctgeom::to_tigre(g, v, 2850, 2850);
    CHECK(close(t.DSD, 1123.142) && close(t.DSO, 152.9016), "TIGRE DSD/DSO equal SDD/SOD in mm");
    CHECK(close(t.dDetector_x, 0.15) && close(t.dVoxel, v), "TIGRE dDetector and dVoxel in mm");

    CHECK(throws_invalid([&] { ctgeom::to_astra_cone(g, 0.0, 10, 10); }), "ASTRA: zero voxel size throws");
    CHECK(throws_invalid([&] { ctgeom::to_tigre(g, v, 0, 10); }), "TIGRE: zero detector rows throws");
}

int main() {
    test_sample_bilinear();
    test_edge_noise_free_dark_sphere();
    test_edge_polarity_agnostic();
    test_edge_with_noise();
    test_edge_rejects_bad_images();
    test_end_to_end_calibration_from_images();
    test_export_astra_and_tigre();

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
