// Minimal, dependency-free unit tests (no gtest needed for a module
// this small): each test is a function that throws on failure, main()
// runs them all and reports pass/fail counts.
#include "ctgeom/geometry.hpp"
#include "ctgeom/xtekct_parser.hpp"
#include "ctgeom/ctinfo_parser.hpp"
#include "ctgeom/ctprofile_parser.hpp"
#include "ctgeom/compare.hpp"

#include <iostream>
#include <cmath>
#include <stdexcept>
#include <fstream>
#include <cstdio>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::cerr << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; ++g_fail; } \
    else { ++g_pass; } \
} while (0)

static bool close(double a, double b, double rel_tol = 1e-9) {
    return std::fabs(a - b) <= rel_tol * std::max(std::fabs(a), std::fabs(b));
}

// --- 1. Hand-calculated known geometry ---
void test_known_geometry() {
    // SOD=100, SDD=500 -> M=5. Pixel=0.2mm -> voxel=0.04mm.
    ctgeom::Geometry g{100.0, 500.0, 0.2, 0.2};
    CHECK(close(ctgeom::magnification(g), 5.0), "magnification = SDD/SOD");
    CHECK(close(ctgeom::voxel_size_x(g), 0.04), "voxel_size_x = pixel/M");
    CHECK(close(ctgeom::voxel_size_y(g), 0.04), "voxel_size_y = pixel/M");
}

// --- 2. Unity magnification edge case (SOD close to SDD) ---
void test_near_unity_magnification() {
    ctgeom::Geometry g{999.0, 1000.0, 0.1, 0.1};
    double M = ctgeom::magnification(g);
    CHECK(M > 1.0 && M < 1.01, "near-unity magnification stays just above 1");
    CHECK(close(ctgeom::voxel_size_x(g), 0.1 / M), "voxel size consistent with M at near-unity case");
}

// --- 3. Invalid geometry should throw, not silently produce garbage ---
void test_invalid_geometry_throws() {
    bool threw = false;
    try {
        ctgeom::Geometry g{500.0, 100.0, 0.1, 0.1}; // SDD < SOD: physically impossible
        ctgeom::magnification(g);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw, "SDD <= SOD must throw invalid_argument");

    threw = false;
    try {
        ctgeom::Geometry g{0.0, 100.0, 0.1, 0.1}; // SOD = 0
        ctgeom::magnification(g);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw, "SOD = 0 must throw invalid_argument");
}

// --- 4. Off-axis magnification reduces to axis case at delta=0 ---
void test_off_axis_matches_axis_at_zero_offset() {
    ctgeom::Geometry g{150.0, 1000.0, 0.15, 0.15};
    double m_axis = ctgeom::magnification(g);
    double m_offaxis = ctgeom::magnification_off_axis(g, 0.0);
    CHECK(close(m_axis, m_offaxis), "off-axis magnification at delta=0 equals axis magnification");

    // Moving a feature closer to the source (positive delta) must increase magnification.
    double m_closer = ctgeom::magnification_off_axis(g, 10.0);
    CHECK(m_closer > m_axis, "feature closer to source has higher magnification");
}

// --- 5. Round-trip against the real uploaded .xtekct file ---
void test_real_xtekct_file() {
    const char* path = "data/poly_isro_mioni1_fbp_01.xtekct";
    ctgeom::XtekctRecord r = ctgeom::parse_xtekct(path);

    CHECK(close(r.src_to_object, 152.9016, 1e-6), "parsed SrcToObject matches file");
    CHECK(close(r.src_to_detector, 1123.142, 1e-6), "parsed SrcToDetector matches file");
    CHECK(close(r.detector_pixel_size_x, 0.15), "parsed DetectorPixelSizeX matches file");
    CHECK(r.name == "poly_isro_mioni1_fbp", "parsed Name matches file");

    auto report = ctgeom::compare_record(r);
    CHECK(!report.any_flagged, "real scan: calculated voxel size matches Nikon-reported value within tolerance");
    CHECK(report.voxel_size_x.pct_diff < 0.01, "real scan: pct diff is near-zero (sub-0.01%)");
}

// --- 6. Parser throws cleanly on a missing required field ---
void test_parser_missing_field_throws() {
    const char* tmp_path = "/tmp/ctgeom_test_malformed.xtekct";
    {
        std::ofstream out(tmp_path);
        out << "[XTekCT]\n";
        out << "Name=incomplete\n";
        out << "SrcToObject=100.0\n";
        // SrcToDetector deliberately omitted
    }
    bool threw = false;
    try {
        ctgeom::parse_xtekct(tmp_path);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw, "missing SrcToDetector must throw runtime_error, not default to 0");
    std::remove(tmp_path);
}

// --- 7. Uncertainty propagation: zero input uncertainty -> zero output uncertainty ---
void test_zero_uncertainty_propagation() {
    ctgeom::Geometry g{150.0, 1000.0, 0.15, 0.15};
    ctgeom::VoxelSizeUncertaintyInputs u{0.0, 0.0, 0.0};
    double sigma = ctgeom::voxel_size_sigma_x(g, u);
    CHECK(close(sigma, 0.0, 1e-12), "zero input uncertainty gives zero output uncertainty");
}

void test_nonzero_uncertainty_propagation_is_positive() {
    ctgeom::Geometry g{150.0, 1000.0, 0.15, 0.15};
    ctgeom::VoxelSizeUncertaintyInputs u{0.05, 0.1, 0.0001}; // mm
    double sigma = ctgeom::voxel_size_sigma_x(g, u);
    CHECK(sigma > 0.0, "nonzero input uncertainty gives positive output uncertainty");
}

// --- 8. .ctinfo.xml parses correctly against the real uploaded file ---
void test_real_ctinfo_file() {
    const char* path = "data/poly_isro_mioni1_fbp_ctinfo.xml";
    ctgeom::CtinfoRecord ci = ctgeom::parse_ctinfo(path);

    CHECK(ci.identifier == "poly_isro_mioni1_fbp", "parsed Identifier matches file");
    CHECK(ci.system_type == "XT H 225 ST 2x", "parsed SystemType matches file");
    CHECK(close(ci.tube_voltage_kv, 200.0), "parsed TubeVoltageInkV matches file");
    CHECK(ci.projections == 360, "parsed Projections matches file");
    CHECK(close(ci.detector_pixel_size_um, 150.0), "parsed DetectorPixelSizeMicrometers matches file");
    CHECK(close(ci.detector_pixel_size_mm(), 0.15), "detector_pixel_size_mm() correctly converts um -> mm");
    CHECK(close(ci.magnification, 7.345523741059397, 1e-6), "parsed Magnification matches file");
    CHECK(close(ci.source_to_object_mm, 152.9015503, 1e-6), "parsed SourceToObjectMM matches file");
    CHECK(close(ci.source_to_detector_mm, 1123.1419677734375, 1e-6), "parsed SourceToDetectorMM matches file");
}

// --- 9. Cross-check: .xtekct and .ctinfo.xml, from the same real scan, must agree ---
void test_cross_check_real_files_agree() {
    ctgeom::XtekctRecord xt = ctgeom::parse_xtekct("data/poly_isro_mioni1_fbp_01.xtekct");
    ctgeom::CtinfoRecord ci = ctgeom::parse_ctinfo("data/poly_isro_mioni1_fbp_ctinfo.xml");

    auto cross = ctgeom::cross_check(xt, ci);
    CHECK(cross.identifier_matches, "xtekct Name and ctinfo Identifier match for the same scan");
    CHECK(!cross.sod.flagged, "SOD agrees between .xtekct and .ctinfo.xml");
    CHECK(!cross.sdd.flagged, "SDD agrees between .xtekct and .ctinfo.xml");
    CHECK(!cross.detector_pixel.flagged, "detector pixel size agrees between .xtekct and .ctinfo.xml (after um->mm conversion)");
    CHECK(!cross.magnification.flagged, "our calculated magnification agrees with ctinfo's reported magnification");
    CHECK(!cross.any_flagged, "overall cross-check passes for a genuinely matching pair of report files");
}

// --- 10. Cross-check: mismatched files (different scans) must be caught ---
void test_cross_check_detects_mismatch() {
    ctgeom::XtekctRecord xt = ctgeom::parse_xtekct("data/poly_isro_mioni1_fbp_01.xtekct");

    // Build a synthetic CtinfoRecord as if it came from a DIFFERENT scan
    // (different SOD), to prove the cross-check actually catches a real
    // disagreement rather than always passing.
    ctgeom::CtinfoRecord ci_wrong;
    ci_wrong.identifier = "poly_isro_mioni1_fbp";
    ci_wrong.detector_pixel_size_um = 150.0;
    ci_wrong.magnification = 7.345523741059397;
    ci_wrong.source_to_object_mm = 200.0;      // deliberately wrong (real value is 152.9)
    ci_wrong.source_to_detector_mm = 1123.1419677734375;

    auto cross = ctgeom::cross_check(xt, ci_wrong);
    CHECK(cross.sod.flagged, "cross-check correctly flags a deliberately mismatched SOD");
    CHECK(cross.any_flagged, "overall cross-check reports flagged when one field disagrees");
}

// --- 11. ctinfo parser throws cleanly on a missing required field ---
void test_ctinfo_parser_missing_field_throws() {
    const char* tmp_path = "/tmp/ctgeom_test_malformed_ctinfo.xml";
    {
        std::ofstream out(tmp_path);
        out << "<?xml version=\"1.0\"?>\n";
        out << "<Information Version=\"2\">\n";
        out << "  <Identifier>incomplete</Identifier>\n";
        out << "  <ScanInformation>\n";
        out << "    <TubeVoltageInkV>200</TubeVoltageInkV>\n";
        // Deliberately omit SourceToObjectMM / SourceToDetectorMM
        out << "  </ScanInformation>\n";
        out << "</Information>\n";
    }
    bool threw = false;
    try {
        ctgeom::parse_ctinfo(tmp_path);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw, "missing SourceToObjectMM/SourceToDetectorMM must throw runtime_error, not default to 0");
    std::remove(tmp_path);
}

// --- 12. .ctprofile.xml parses correctly against the real uploaded file ---
void test_real_ctprofile_file() {
    const char* path = "data/poly_isro_mioni1_fbp_ctprofile.xml";
    ctgeom::CtprofileRecord cp = ctgeom::parse_ctprofile(path);

    CHECK(cp.profile_name == "poly_isro_mioni1_fbp", "parsed ProfileName matches file");
    CHECK(close(cp.detector_pixel_size_mm, 0.15), "parsed DetectorPixelSizeMM matches file");
    CHECK(cp.has_voi, "VolumeOfInterest block was found and parsed");
    CHECK(cp.voi.x_start == 995 && cp.voi.x_end == 2020, "parsed VOI XStart/XEnd match file");
    CHECK(cp.voi.y_start == 873 && cp.voi.y_end == 1847, "parsed VOI YStart/YEnd match file");
    CHECK(cp.voi.z_start == 0 && cp.voi.z_end == 2849, "parsed VOI ZStart/ZEnd match file");
}

// --- 13. Real-data proof: ctprofile's VOI crop exactly explains xtekct's voxel dimensions ---
// This is the actual new finding this parser was built to surface: the
// reconstruction's pixel-space crop region, recorded independently in
// .ctprofile.xml, exactly accounts for the VoxelsX/Y/Z Nikon wrote into
// the separate .xtekct file for the same job.
void test_voi_explains_xtekct_voxel_dimensions() {
    ctgeom::XtekctRecord xt = ctgeom::parse_xtekct("data/poly_isro_mioni1_fbp_01.xtekct");
    ctgeom::CtprofileRecord cp = ctgeom::parse_ctprofile("data/poly_isro_mioni1_fbp_ctprofile.xml");

    CHECK(xt.voxels_x == 1026, "sanity: xtekct VoxelsX is 1026 as uploaded");
    CHECK(xt.voxels_y == 975, "sanity: xtekct VoxelsY is 975 as uploaded");
    CHECK(xt.voxels_z == 2850, "sanity: xtekct VoxelsZ is 2850 as uploaded");

    auto voi = ctgeom::check_voi_consistency(xt, cp);
    CHECK(voi.voxels_x_match, "VOI crop width (XEnd-XStart+1) exactly equals xtekct VoxelsX");
    CHECK(voi.voxels_y_match, "VOI crop height (YEnd-YStart+1) exactly equals xtekct VoxelsY");
    CHECK(voi.voxels_z_match, "VOI crop depth (ZEnd-ZStart+1) exactly equals xtekct VoxelsZ");
    CHECK(voi.all_match, "overall VOI-vs-voxel-dimensions check passes for this real, matching pair");
}

// --- 14. VOI consistency check correctly catches a real mismatch ---
void test_voi_consistency_detects_mismatch() {
    ctgeom::XtekctRecord xt = ctgeom::parse_xtekct("data/poly_isro_mioni1_fbp_01.xtekct");

    // A synthetic ctprofile whose crop does NOT match this xtekct's voxel
    // counts, as if the two files came from different reconstruction jobs
    // (this is exactly the situation flagged earlier when a 1000x1000
    // slice stack didn't match this scan's 1026x975 volume).
    ctgeom::CtprofileRecord cp_wrong;
    cp_wrong.has_voi = true;
    cp_wrong.voi.x_start = 0; cp_wrong.voi.x_end = 999;   // width 1000, not 1026
    cp_wrong.voi.y_start = 0; cp_wrong.voi.y_end = 999;   // height 1000, not 975
    cp_wrong.voi.z_start = 0; cp_wrong.voi.z_end = 999;   // depth 1000, not 2850

    auto voi = ctgeom::check_voi_consistency(xt, cp_wrong);
    CHECK(!voi.voxels_x_match, "mismatched VOI width is correctly caught");
    CHECK(!voi.voxels_y_match, "mismatched VOI height is correctly caught");
    CHECK(!voi.voxels_z_match, "mismatched VOI depth is correctly caught");
    CHECK(!voi.all_match, "overall check correctly reports failure for a mismatched pair");
}

// --- 15. ctprofile parser throws cleanly when VolumeOfInterest is missing ---
void test_ctprofile_missing_voi_throws() {
    const char* tmp_path = "/tmp/ctgeom_test_malformed_ctprofile.xml";
    {
        std::ofstream out(tmp_path);
        out << "<?xml version=\"1.0\"?>\n<CTProfile>\n";
        out << "  <ProfileName>incomplete</ProfileName>\n";
        out << "  <DetectorPixelSizeMM>0.15</DetectorPixelSizeMM>\n";
        // VolumeOfInterest deliberately omitted entirely
        out << "</CTProfile>\n";
    }
    bool threw = false;
    try {
        ctgeom::parse_ctprofile(tmp_path);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw, "missing <VolumeOfInterest> must throw runtime_error, not silently skip the check");
    std::remove(tmp_path);
}

// --- 16. Off-axis uniformity check: zero-size volume means no off-axis effect ---
void test_off_axis_uniformity_zero_for_point_volume() {
    ctgeom::XtekctRecord xt;
    xt.src_to_object = 150.0;
    xt.src_to_detector = 1000.0;
    xt.detector_pixel_size_x = 0.15;
    xt.detector_pixel_size_y = 0.15;
    xt.voxels_x = 0; xt.voxels_y = 0;       // degenerate: zero-width volume
    xt.voxel_size_x = 0.02; xt.voxel_size_y = 0.02;

    auto r = ctgeom::check_off_axis_uniformity(xt);
    CHECK(close(r.fov_half_width_mm, 0.0, 1e-12), "zero voxel count gives zero FOV half-width");
    CHECK(close(r.pct_diff, 0.0, 1e-9), "zero half-width gives zero off-axis magnification difference");
    CHECK(!r.significant, "zero difference is never flagged significant");
}

// --- 17. Off-axis uniformity check: real scan produces a real, non-trivial effect ---
// This is the actual finding the sensitivity analysis surfaced: this
// scan's volume is wide enough (relative to its short SOD) that the
// edge-of-FOV magnification differs noticeably from the axis value.
void test_off_axis_uniformity_real_scan_is_significant() {
    ctgeom::XtekctRecord xt = ctgeom::parse_xtekct("data/poly_isro_mioni1_fbp_01.xtekct");
    auto r = ctgeom::check_off_axis_uniformity(xt);

    double expected_half_width = (1026 * 0.0204206) / 2.0; // ~10.48mm, from this scan's own real dimensions
    CHECK(close(r.fov_half_width_mm, expected_half_width, 1e-3),
          "FOV half-width is computed from this scan's real VoxelsX * VoxelSizeX");
    CHECK(r.magnification_at_edge > r.magnification_axis,
          "a feature at the FOV edge (closer to the source at some point in rotation) magnifies MORE than the axis value");
    CHECK(r.pct_diff > 5.0, "real scan's off-axis effect is large (single-digit percent), matching the sensitivity report's finding");
    CHECK(r.significant, "this scan's off-axis effect exceeds the default 2% significance threshold");
}

// --- 18. Off-axis uniformity check: significance threshold is configurable and respected ---
void test_off_axis_uniformity_threshold_is_respected() {
    ctgeom::XtekctRecord xt = ctgeom::parse_xtekct("data/poly_isro_mioni1_fbp_01.xtekct");

    auto strict = ctgeom::check_off_axis_uniformity(xt, /*significance_threshold_pct=*/1.0);
    auto lenient = ctgeom::check_off_axis_uniformity(xt, /*significance_threshold_pct=*/50.0);

    CHECK(strict.significant, "a strict (low) threshold flags this scan's real off-axis effect");
    CHECK(!lenient.significant, "a very lenient (high) threshold does not flag the same effect");
    CHECK(close(strict.pct_diff, lenient.pct_diff), "the underlying computed effect itself doesn't change with the threshold, only the flag");
}

int main() {
    test_known_geometry();
    test_near_unity_magnification();
    test_invalid_geometry_throws();
    test_off_axis_matches_axis_at_zero_offset();
    test_real_xtekct_file();
    test_parser_missing_field_throws();
    test_zero_uncertainty_propagation();
    test_nonzero_uncertainty_propagation_is_positive();
    test_real_ctinfo_file();
    test_cross_check_real_files_agree();
    test_cross_check_detects_mismatch();
    test_ctinfo_parser_missing_field_throws();
    test_real_ctprofile_file();
    test_voi_explains_xtekct_voxel_dimensions();
    test_voi_consistency_detects_mismatch();
    test_ctprofile_missing_voi_throws();
    test_off_axis_uniformity_zero_for_point_volume();
    test_off_axis_uniformity_real_scan_is_significant();
    test_off_axis_uniformity_threshold_is_respected();

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
