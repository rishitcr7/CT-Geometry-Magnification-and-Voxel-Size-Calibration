// ctgeom::geometry
//
// Core cone-beam CT geometry relationships: magnification, effective
// voxel size, and first-order error propagation.
//
// Conventions follow ASTRA/TIGRE style naming:
//   SOD (source_origin)  = source-to-object distance   (source to rotation axis)
//   SDD (origin_det+SOD) = source-to-detector distance (source to detector plane)
// All distances are expected in consistent physical units (e.g. mm).
#pragma once

#include <stdexcept>
#include <cmath>

namespace ctgeom {

// A minimal description of acquisition geometry, enough to compute
// magnification and effective voxel size at the rotation axis.
struct Geometry {
    double sod = 0.0;          // source-to-object distance
    double sdd = 0.0;          // source-to-detector distance
    double det_pixel_x = 0.0;  // physical detector pixel pitch, X (object-space units, e.g. mm)
    double det_pixel_y = 0.0;  // physical detector pixel pitch, Y

    void validate() const {
        if (sod <= 0.0) throw std::invalid_argument("Geometry: SOD must be > 0");
        if (sdd <= 0.0) throw std::invalid_argument("Geometry: SDD must be > 0");
        if (sdd <= sod) throw std::invalid_argument("Geometry: SDD must be > SOD (detector is behind the object)");
        if (det_pixel_x <= 0.0) throw std::invalid_argument("Geometry: det_pixel_x must be > 0");
        if (det_pixel_y <= 0.0) throw std::invalid_argument("Geometry: det_pixel_y must be > 0");
    }
};

// Geometric magnification at the rotation axis: M = SDD / SOD.
inline double magnification(const Geometry& g) {
    g.validate();
    return g.sdd / g.sod;
}

// Effective object-side voxel size along X: V = det_pixel_x / M.
inline double voxel_size_x(const Geometry& g) {
    return g.det_pixel_x / magnification(g);
}

// Effective object-side voxel size along Y: V = det_pixel_y / M.
inline double voxel_size_y(const Geometry& g) {
    return g.det_pixel_y / magnification(g);
}

// Magnification of a feature offset from the rotation axis by
// delta_from_axis along the beam direction (object moved delta_from_axis
// closer to the source is delta_from_axis > 0 meaning SOD decreases).
// This generalises M = SDD/SOD to off-axis features: a feature sitting at
// true source-to-feature distance (SOD - delta) magnifies by
// SDD / (SOD - delta).
inline double magnification_off_axis(const Geometry& g, double delta_from_axis) {
    g.validate();
    const double sod_feature = g.sod - delta_from_axis;
    if (sod_feature <= 0.0)
        throw std::invalid_argument("magnification_off_axis: feature is at or behind the source");
    return g.sdd / sod_feature;
}

// First-order (linear) propagation of parameter uncertainty into voxel
// size uncertainty, via partial derivatives of V = p / (SDD/SOD) = p*SOD/SDD.
//   dV/dp   = SOD/SDD
//   dV/dSOD = p/SDD
//   dV/dSDD = -p*SOD/SDD^2
// Inputs are 1-sigma (or otherwise consistent) uncertainties in each
// parameter; independence of errors (no covariance) is assumed, i.e. a
// standard root-sum-square combination.
struct VoxelSizeUncertaintyInputs {
    double sigma_sod = 0.0;
    double sigma_sdd = 0.0;
    double sigma_pixel = 0.0;
};

inline double voxel_size_sigma_x(const Geometry& g, const VoxelSizeUncertaintyInputs& u) {
    g.validate();
    const double p = g.det_pixel_x;
    const double dV_dp   = g.sod / g.sdd;
    const double dV_dSOD = p / g.sdd;
    const double dV_dSDD = -p * g.sod / (g.sdd * g.sdd);
    const double term_p   = dV_dp   * u.sigma_pixel;
    const double term_sod = dV_dSOD * u.sigma_sod;
    const double term_sdd = dV_dSDD * u.sigma_sdd;
    return std::sqrt(term_p * term_p + term_sod * term_sod + term_sdd * term_sdd);
}

} // namespace ctgeom
