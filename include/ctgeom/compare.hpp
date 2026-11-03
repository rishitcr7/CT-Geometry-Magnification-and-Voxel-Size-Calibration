// ctgeom::compare
//
// Compares calculated geometry/voxel-size values against the values a
// Nikon .xtekct report already carries, and flags disagreements that
// exceed an expected-tolerance threshold. Also cross-checks two
// independent Nikon report files (.xtekct and .ctinfo.xml) against each
// other, since they are generated separately by different parts of the
// Nikon software and should agree if the scan metadata is self-consistent.
#pragma once

#include "geometry.hpp"
#include "xtekct_parser.hpp"
#include "ctinfo_parser.hpp"
#include "ctprofile_parser.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace ctgeom {

struct FieldComparison {
    std::string field_name;
    double calculated = 0.0;
    double reported = 0.0;
    double abs_diff = 0.0;
    double pct_diff = 0.0;   // percentage, relative to reported value
    bool flagged = false;    // true if pct_diff exceeds the tolerance
};

struct ComparisonReport {
    FieldComparison voxel_size_x;
    bool any_flagged = false;
};

// tolerance_pct: percentage threshold above which a difference is flagged
// (default 0.5% — generous enough to absorb float rounding in the source
// report, tight enough to catch a real geometry mismatch; tune once more
// real scans have been checked, per the open question in the Plan of
// Action about expected-vs-flagged discrepancy).
inline FieldComparison compare_field(const std::string& name, double calculated, double reported,
                                      double tolerance_pct = 0.5) {
    FieldComparison c;
    c.field_name = name;
    c.calculated = calculated;
    c.reported = reported;
    c.abs_diff = calculated - reported;
    c.pct_diff = (reported != 0.0) ? std::fabs(c.abs_diff) / std::fabs(reported) * 100.0
                                    : std::fabs(c.abs_diff) * 100.0;
    c.flagged = c.pct_diff > tolerance_pct;
    return c;
}

// Runs the standard set of checks for one parsed .xtekct record:
// recomputes voxel size from (SOD, SDD, detector pixel pitch) and
// compares it against the VoxelSizeX Nikon already wrote to the file.
inline ComparisonReport compare_record(const XtekctRecord& r, double tolerance_pct = 0.5) {
    Geometry g = r.to_geometry();
    double calc_vx = voxel_size_x(g);

    ComparisonReport report;
    report.voxel_size_x = compare_field("voxel_size_x", calc_vx, r.voxel_size_x, tolerance_pct);
    report.any_flagged = report.voxel_size_x.flagged;
    return report;
}

// --- Cross-checking two independent report files against each other ---
//
// .xtekct and .ctinfo.xml are written by different parts of the Nikon
// software from (in principle) the same underlying scan metadata. If a
// scan was re-centred, reconstructed with different settings, or if the
// two files somehow come from different jobs, these can disagree even
// though each file is internally consistent. This is the independent
// cross-check Section 9 of the Plan of Action calls for — it does not
// depend on our own calculated values at all, just on whether Nikon's
// own two files agree with each other.
struct CrossCheckReport {
    FieldComparison sod;              // XtekctRecord.src_to_object vs CtinfoRecord.source_to_object_mm
    FieldComparison sdd;              // XtekctRecord.src_to_detector vs CtinfoRecord.source_to_detector_mm
    FieldComparison detector_pixel;   // XtekctRecord.detector_pixel_size_x vs CtinfoRecord.detector_pixel_size_mm
    FieldComparison magnification;    // our calculated M (from .xtekct geometry) vs CtinfoRecord.magnification
    bool identifier_matches = false;  // XtekctRecord.name == CtinfoRecord.identifier
    bool any_flagged = false;
};

inline CrossCheckReport cross_check(const XtekctRecord& xt, const CtinfoRecord& ci,
                                     double tolerance_pct = 0.01) {
    // Tolerance here defaults much tighter than compare_record's 0.5%:
    // these two files describe the exact same acquisition, so beyond
    // float rounding they should match almost exactly. A mismatch above
    // ~0.01% more likely means the files are from different jobs/runs
    // than that it's ordinary numerical noise.
    CrossCheckReport r;
    r.sod = compare_field("sod_xtekct_vs_ctinfo", xt.src_to_object, ci.source_to_object_mm, tolerance_pct);
    r.sdd = compare_field("sdd_xtekct_vs_ctinfo", xt.src_to_detector, ci.source_to_detector_mm, tolerance_pct);
    r.detector_pixel = compare_field("detector_pixel_xtekct_vs_ctinfo",
                                      xt.detector_pixel_size_x, ci.detector_pixel_size_mm(), tolerance_pct);

    Geometry g = xt.to_geometry();
    double calc_M = magnification(g);
    r.magnification = compare_field("magnification_calculated_vs_ctinfo_reported", calc_M, ci.magnification, tolerance_pct);

    r.identifier_matches = (xt.name == ci.identifier);

    r.any_flagged = r.sod.flagged || r.sdd.flagged || r.detector_pixel.flagged || r.magnification.flagged;
    return r;
}

// --- Cross-checking .xtekct voxel dimensions against .ctprofile.xml's crop ---
//
// .ctprofile.xml records the pixel-space crop (VolumeOfInterest) that
// was applied to the full detector image before reconstruction. The
// resulting volume's voxel counts in .xtekct should exactly equal that
// crop's width/height/depth:
//   VoxelsX == XEnd - XStart + 1   (and similarly Y, Z)
// This is an EXACT integer check, not a percentage tolerance — these
// are pixel counts, not physical measurements with rounding, so any
// disagreement at all means the two files describe different
// reconstruction jobs (or the crop was changed after .xtekct was
// written), which is worth surfacing immediately rather than averaging
// away in a percentage threshold.
struct VoiConsistencyReport {
    bool voxels_x_match = false;
    bool voxels_y_match = false;
    bool voxels_z_match = false;
    int expected_voxels_x = 0, actual_voxels_x = 0;
    int expected_voxels_y = 0, actual_voxels_y = 0;
    int expected_voxels_z = 0, actual_voxels_z = 0;
    bool all_match = false;
};

inline VoiConsistencyReport check_voi_consistency(const XtekctRecord& xt, const CtprofileRecord& cp) {
    VoiConsistencyReport r;
    r.expected_voxels_x = cp.voi.width();
    r.expected_voxels_y = cp.voi.height();
    r.expected_voxels_z = cp.voi.depth();
    r.actual_voxels_x = xt.voxels_x;
    r.actual_voxels_y = xt.voxels_y;
    r.actual_voxels_z = xt.voxels_z;

    r.voxels_x_match = (r.expected_voxels_x == r.actual_voxels_x);
    r.voxels_y_match = (r.expected_voxels_y == r.actual_voxels_y);
    r.voxels_z_match = (r.expected_voxels_z == r.actual_voxels_z);
    r.all_match = r.voxels_x_match && r.voxels_y_match && r.voxels_z_match;
    return r;
}

// --- Off-axis magnification uniformity check ---
//
// voxel_size_x()/voxel_size_y() use the AXIS magnification (M=SDD/SOD)
// to assign one physical size to every voxel in the volume. That is an
// approximation: a feature away from the rotation axis sweeps through
// a range of true source-distances as the object rotates during a scan
// (at radius r from the axis, instantaneous distance-to-source ranges
// from SOD-r to SOD+r), so its effective magnification differs from the
// axis value. magnification_off_axis() quantifies this for a given
// radial offset; this check applies it at the field-of-view's own
// half-width (computed from the volume's own reported dimensions) to
// answer: "how much does the single-voxel-size assumption break down at
// the EDGE of this particular scan's volume?"
//
// This is not a pass/fail correctness check the way compare_record() or
// cross_check() are — a nonzero value here is expected cone-beam
// geometry, not an error in any file. `significant` just flags when
// that expected effect is large enough (default >2%) that anyone doing
// precise measurement near the volume's edge should know the single
// global voxel size is a worse approximation there than at the centre.
struct OffAxisUniformityReport {
    double fov_half_width_mm = 0.0;     // radius used, from the volume's own reported dimensions
    double magnification_axis = 0.0;
    double magnification_at_edge = 0.0;
    double pct_diff = 0.0;              // edge vs axis magnification, %
    bool significant = false;           // true if |pct_diff| exceeds significance_threshold_pct
};

inline OffAxisUniformityReport check_off_axis_uniformity(const XtekctRecord& xt,
                                                           double significance_threshold_pct = 2.0) {
    Geometry g = xt.to_geometry();

    // Half-width of the reconstructed volume in each direction, using
    // Nikon's own reported voxel size (consistent with how the volume
    // was actually written out, independent of whether our own
    // calculated voxel size agrees with it).
    double half_width_x = (xt.voxels_x * xt.voxel_size_x) / 2.0;
    double half_width_y = (xt.voxels_y * xt.voxel_size_y) / 2.0;
    double radius = std::max(half_width_x, half_width_y);

    OffAxisUniformityReport r;
    r.fov_half_width_mm = radius;
    r.magnification_axis = magnification(g);
    r.magnification_at_edge = magnification_off_axis(g, radius);
    r.pct_diff = (r.magnification_at_edge - r.magnification_axis) / r.magnification_axis * 100.0;
    r.significant = std::fabs(r.pct_diff) > significance_threshold_pct;
    return r;
}

// --- Magnification-aware tolerance (opt-in; defaults elsewhere unchanged) ---
//
// Since V = p * SOD / SDD, a positioning uncertainty sigma_pos in SOD
// changes V by the relative amount sigma_pos / SOD. SOD gets small at high
// magnification, so the same mechanical error is a bigger fraction of it.
// This returns a tolerance that is never below `base_pct` and otherwise
// equals k_sigma * 100 * sigma_pos / SOD.
//
// IMPORTANT caveats:
//  * sigma_pos_mm = 0.05 is an ILLUSTRATIVE placeholder, not a measured
//    figure for any scanner. Replace it with a real stage-repeatability
//    value before relying on the result.
//  * It is only meaningful where two SOD values from different moments
//    are being compared (e.g. a re-centred scan). compare_record() uses
//    SOD and voxel size from the SAME file, so positioning error cancels
//    there and the fixed default tolerance is already adequate.
inline double scaled_tolerance_pct(const Geometry& g, double sigma_pos_mm = 0.05,
                                    double k_sigma = 3.0, double base_pct = 0.5) {
    g.validate();
    if (sigma_pos_mm < 0.0 || k_sigma < 0.0 || base_pct < 0.0)
        throw std::invalid_argument("scaled_tolerance_pct: arguments must be >= 0");
    const double positioning_pct = k_sigma * 100.0 * sigma_pos_mm / g.sod;
    return std::max(base_pct, positioning_pct);
}

// Same as cross_check(), but the tolerance comes from scaled_tolerance_pct()
// for this scan's own geometry.
inline CrossCheckReport cross_check_scaled(const XtekctRecord& xt, const CtinfoRecord& ci,
                                            double sigma_pos_mm = 0.05, double k_sigma = 3.0) {
    const double tol = std::max(0.01, scaled_tolerance_pct(xt.to_geometry(), sigma_pos_mm, k_sigma, 0.01));
    return cross_check(xt, ci, tol);
}

} // namespace ctgeom
