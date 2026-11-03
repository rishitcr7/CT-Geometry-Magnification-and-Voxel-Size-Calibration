// ctgeom::image_edge
//
// Sub-pixel silhouette-edge finder for a reference sphere in a projection
// image, producing the edge points that calibration.hpp consumes.
//
// Method: cast rays outward from an approximate centre, sample the image
// along each ray with bilinear interpolation, smooth lightly, find the
// steepest intensity change, then refine to the half-contrast crossing by
// linear interpolation. Works for a dark sphere on a bright background or
// the reverse (polarity is not assumed).
//
// Assumptions (checked where possible, otherwise documented):
//   * The approximate centre lies inside the sphere's silhouette.
//   * The silhouette edge lies between 20% and 85% of max_radius_px from
//     that centre: the first 20% of each ray is taken as "inside" level and
//     the last 15% as "outside" level.
//   * The image is a single-channel projection with a roughly uniform
//     background near the sphere.
//
// Tested ONLY on synthetic images (render_disc_phantom below). Real
// projections have beam hardening, scatter, a non-flat background and the
// sphere's cone-beam shape, none of which are modelled here; accuracy on
// real data is unknown until a real reference-sphere scan is available.
#pragma once

#include "calibration.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <vector>

namespace ctgeom {

// Single-channel image. Pixel (ix, iy) has its centre at coordinates (ix, iy).
struct Image {
    size_t width = 0;
    size_t height = 0;
    std::vector<float> data;  // row-major, size width * height

    Image() = default;
    Image(size_t w, size_t h, float fill = 0.0f) : width(w), height(h), data(w * h, fill) {}

    float at(size_t x, size_t y) const { return data[y * width + x]; }
    float& at(size_t x, size_t y) { return data[y * width + x]; }

    // Bilinear sample at real-valued (x, y); throws if outside the image.
    double sample(double x, double y) const {
        if (x < 0.0 || y < 0.0 || x > static_cast<double>(width - 1) || y > static_cast<double>(height - 1))
            throw std::out_of_range("Image::sample: coordinates outside the image");
        size_t x0 = static_cast<size_t>(std::floor(x));
        size_t y0 = static_cast<size_t>(std::floor(y));
        size_t x1 = std::min(x0 + 1, width - 1);
        size_t y1 = std::min(y0 + 1, height - 1);
        double fx = x - static_cast<double>(x0), fy = y - static_cast<double>(y0);
        double top = at(x0, y0) * (1 - fx) + at(x1, y0) * fx;
        double bot = at(x0, y1) * (1 - fx) + at(x1, y1) * fx;
        return top * (1 - fy) + bot * fy;
    }
};

struct EdgeFinderOptions {
    size_t n_rays = 360;
    double step_px = 0.25;           // sampling step along each ray
    double min_contrast_to_noise = 8.0;  // reject a ray if |inside - outside| < this * inside-noise
};

// Finds edge points on the sphere silhouette. Throws std::invalid_argument
// if too few rays give a trustworthy edge (low contrast, edge out of range).
inline std::vector<Point2D> find_sphere_edge_points(const Image& img, double approx_cx, double approx_cy,
                                                     double max_radius_px,
                                                     const EdgeFinderOptions& opt = EdgeFinderOptions()) {
    if (img.width < 3 || img.height < 3) throw std::invalid_argument("find_sphere_edge_points: image too small");
    if (max_radius_px <= 5.0) throw std::invalid_argument("find_sphere_edge_points: max_radius_px too small");
    if (opt.n_rays < 8) throw std::invalid_argument("find_sphere_edge_points: need at least 8 rays");
    if (opt.step_px <= 0.0) throw std::invalid_argument("find_sphere_edge_points: step must be > 0");

    const double pi = 3.14159265358979323846;
    const size_t ns = static_cast<size_t>(max_radius_px / opt.step_px);
    if (ns < 20) throw std::invalid_argument("find_sphere_edge_points: ray too short for the step size");
    const size_t in_end = ns / 5;               // first 20% = inside level
    const size_t out_start = ns - ns * 15 / 100; // last 15% = outside level

    std::vector<Point2D> pts;
    std::vector<double> prof(ns), sm(ns);

    for (size_t k = 0; k < opt.n_rays; ++k) {
        const double th = 2.0 * pi * static_cast<double>(k) / static_cast<double>(opt.n_rays);
        const double ct = std::cos(th), st = std::sin(th);
        for (size_t i = 0; i < ns; ++i) {
            const double r = static_cast<double>(i) * opt.step_px;
            prof[i] = img.sample(approx_cx + r * ct, approx_cy + r * st);  // throws if off-image
        }

        // Levels and inside-noise estimate.
        double in_mean = 0, out_mean = 0;
        for (size_t i = 0; i < in_end; ++i) in_mean += prof[i];
        in_mean /= static_cast<double>(in_end);
        for (size_t i = out_start; i < ns; ++i) out_mean += prof[i];
        out_mean /= static_cast<double>(ns - out_start);
        double var = 0;
        for (size_t i = 0; i < in_end; ++i) var += (prof[i] - in_mean) * (prof[i] - in_mean);
        const double noise = std::sqrt(var / static_cast<double>(in_end)) + 1e-12;
        const double contrast = out_mean - in_mean;
        if (std::fabs(contrast) < opt.min_contrast_to_noise * noise) continue;  // untrustworthy ray

        // 5-tap box smoothing, then the steepest change inside [20%, 85%].
        for (size_t i = 0; i < ns; ++i) {
            double s = 0; int c = 0;
            for (int d = -2; d <= 2; ++d) {
                long j = static_cast<long>(i) + d;
                if (j >= 0 && j < static_cast<long>(ns)) { s += prof[static_cast<size_t>(j)]; ++c; }
            }
            sm[i] = s / c;
        }
        size_t best = in_end;
        double best_g = -1.0;
        for (size_t i = in_end; i + 1 < out_start; ++i) {
            const double g = (sm[i + 1] - sm[i]) * (contrast > 0 ? 1.0 : -1.0);  // rises toward the outside level
            if (g > best_g) { best_g = g; best = i; }
        }
        if (best_g <= 0.0) continue;

        // Half-contrast crossing near the steepest point (linear interpolation).
        const double half = 0.5 * (in_mean + out_mean);
        const size_t lo = best > 6 ? best - 6 : in_end;
        const size_t hi = std::min(best + 7, out_start);
        bool found = false;
        double r_edge = 0;
        for (size_t i = std::max(lo, in_end); i + 1 < hi; ++i) {
            const double a = sm[i] - half, b = sm[i + 1] - half;
            if ((a <= 0 && b > 0) || (a >= 0 && b < 0)) {
                const double t = a / (a - b);
                r_edge = (static_cast<double>(i) + t) * opt.step_px;
                found = true;
                break;
            }
        }
        if (!found) continue;
        pts.push_back({approx_cx + r_edge * ct, approx_cy + r_edge * st});
    }

    if (pts.size() < opt.n_rays / 2)
        throw std::invalid_argument("find_sphere_edge_points: fewer than half the rays found a clear edge "
                                    "(low contrast, centre outside the sphere, or edge outside the 20%-85% range)");
    return pts;
}

// --- Synthetic projection image for testing --------------------------------
//
// A disc of radius radius_px whose edge is blurred with a Gaussian of
// standard deviation blur_sigma_px (so the 50% intensity contour sits
// exactly at radius_px), with optional Gaussian pixel noise. Deterministic
// for a given seed. Intensity is `inside` within the disc, `outside` away
// from it.
inline Image render_disc_phantom(size_t width, size_t height, double cx, double cy, double radius_px,
                                  float inside, float outside, double blur_sigma_px,
                                  double noise_sigma, unsigned seed) {
    if (blur_sigma_px <= 0.0) throw std::invalid_argument("render_disc_phantom: blur sigma must be > 0");
    Image img(width, height);
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, noise_sigma > 0.0 ? noise_sigma : 1.0);
    const double s2 = blur_sigma_px * std::sqrt(2.0);
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            const double d = std::hypot(static_cast<double>(x) - cx, static_cast<double>(y) - cy);
            const double w = 0.5 * std::erfc((d - radius_px) / s2);  // 1 inside, 0 outside, 0.5 at the edge
            double v = outside + (inside - outside) * w;
            if (noise_sigma > 0.0) v += noise(rng);
            img.at(x, y) = static_cast<float>(v);
        }
    }
    return img;
}

} // namespace ctgeom
