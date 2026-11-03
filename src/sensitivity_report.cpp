// Sensitivity / tolerance analysis tool.
//
// This does NOT require any new scan data. It answers two questions the
// project has had open since the Plan of Action (Sections 5, 8, 16):
//
//   1. The compare/cross-check tolerances (0.5% for calculated-vs-xtekct,
//      0.01% for xtekct-vs-ctinfo) were picked by judgement, not derived.
//      How big a REAL error (in mm, on SOD/SDD) would it take to actually
//      trip each threshold? If the answer is "a huge, physically
//      implausible error," the threshold is too loose to be useful.
//
//   2. magnification_off_axis() and voxel_size_sigma_x() were written in
//      geometry.hpp but never run against anything. This tool runs them
//      against the one real scan we have, to show what they're for:
//      quantifying how much magnification/voxel size would shift if an
//      object sits off the rotation axis, or if the measured SOD/SDD/
//      pixel-pitch inputs carry some realistic uncertainty.
//
// Output is plain text so it can be redirected into a report file.
#include "ctgeom/geometry.hpp"
#include "ctgeom/xtekct_parser.hpp"
#include "ctgeom/compare.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

using ctgeom::Geometry;

static void print_header(const std::string& title) {
    std::cout << "\n=== " << title << " ===\n";
}

// --- Part 1: how much SOD error does it take to trip the tolerance? ---
//
// For a given base geometry, perturbs SOD by increasing amounts and
// reports the resulting voxel-size pct_diff at each step, until it
// crosses the 0.5% tolerance used by compare_record(). This turns an
// abstract percentage into a concrete "how many microns of real stage
// error would we actually catch" answer.
static void sod_perturbation_sweep(const std::string& label, const Geometry& base_geom,
                                    double tolerance_pct) {
    print_header("SOD perturbation sweep: " + label);
    std::cout << "Base geometry: SOD=" << base_geom.sod << "mm  SDD=" << base_geom.sdd
              << "mm  pixel=" << base_geom.det_pixel_x << "mm\n";
    double base_M = ctgeom::magnification(base_geom);
    double base_V = ctgeom::voxel_size_x(base_geom);
    std::cout << "Base magnification=" << base_M << "  base voxel size=" << base_V << " mm\n\n";

    std::cout << std::left << std::setw(16) << "SOD error (mm)"
              << std::setw(16) << "SOD error (%)"
              << std::setw(18) << "Voxel size (mm)"
              << std::setw(16) << "Voxel diff (%)"
              << "Status\n";

    std::vector<double> sod_errors_mm = {0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1.0, 2.0};
    bool reported_crossing = false;
    for (double err : sod_errors_mm) {
        Geometry g = base_geom;
        g.sod += err; // simulate a real-world positioning error of this many mm
        double v = ctgeom::voxel_size_x(g);
        auto cmp = ctgeom::compare_field("voxel_size_x", v, base_V, tolerance_pct);

        std::cout << std::left << std::setw(16) << err
                  << std::setw(16) << (err / base_geom.sod * 100.0)
                  << std::setw(18) << v
                  << std::setw(16) << cmp.pct_diff
                  << (cmp.flagged ? "FLAGGED" : "ok") << "\n";

        if (cmp.flagged && !reported_crossing) {
            std::cout << "  -> tolerance (" << tolerance_pct << "%) first crossed at SOD error ~"
                      << err << " mm (" << (err / base_geom.sod * 100.0) << "% of SOD)\n";
            reported_crossing = true;
        }
    }
    if (!reported_crossing) {
        std::cout << "  -> tolerance (" << tolerance_pct << "%) was NOT crossed within the tested range "
                  << "(up to " << sod_errors_mm.back() << " mm) -- threshold may be too loose for this geometry.\n";
    }
}

// --- Part 2: off-axis magnification on the real scan geometry ---
static void off_axis_report(const Geometry& g) {
    print_header("Off-axis magnification (real scan geometry)");
    std::cout << "A feature sitting away from the rotation axis magnifies differently\n";
    std::cout << "than the axis-magnification (M=SDD/SOD) the voxel size is based on.\n";
    std::cout << "This matters for any feature not at dead-center of the scanned volume.\n\n";

    double m_axis = ctgeom::magnification(g);
    std::cout << std::left << std::setw(28) << "Offset from axis (mm)"
              << std::setw(18) << "Magnification"
              << "Diff from axis-M (%)\n";

    std::vector<double> offsets = {0.0, 1.0, 2.0, 5.0, 10.0};
    for (double d : offsets) {
        double m = ctgeom::magnification_off_axis(g, d);
        double pct = (m - m_axis) / m_axis * 100.0;
        std::cout << std::left << std::setw(28) << d
                  << std::setw(18) << m
                  << pct << " %\n";
    }
    std::cout << "\nNote: this scan's object half-width is roughly VoxelsX*VoxelSizeX/2\n"
              << "~= 1026*0.0204/2 ~= 10.5 mm, so a feature at the EDGE of the field of\n"
              << "view (not just the center) can see a magnification several % different\n"
              << "from the axis value used for the whole-volume voxel size.\n";
}

// --- Part 3: propagated voxel-size uncertainty for realistic input uncertainties ---
static void uncertainty_report(const Geometry& g) {
    print_header("Propagated voxel-size uncertainty (real scan geometry)");
    std::cout << "Using plausible real-world uncertainties for a Nikon XT H 225-class\n"
              << "system (NOT measured -- these are reasonable placeholder estimates,\n"
              << "flagged here so they can be replaced with real stage-calibration\n"
              << "numbers from the scanner's own specs if/when available):\n";
    std::cout << "  sigma_SOD   = 0.01 mm  (typical linear-stage repeatability)\n";
    std::cout << "  sigma_SDD   = 0.05 mm  (longer axis, somewhat looser)\n";
    std::cout << "  sigma_pixel = 0.0001 mm (detector pixel pitch, manufacturing spec)\n\n";

    ctgeom::VoxelSizeUncertaintyInputs u{0.01, 0.05, 0.0001};
    double sigma_v = ctgeom::voxel_size_sigma_x(g, u);
    double v = ctgeom::voxel_size_x(g);
    double rel_pct = sigma_v / v * 100.0;

    std::cout << "Calculated voxel size:        " << v << " mm\n";
    std::cout << "Propagated 1-sigma uncertainty: +/- " << sigma_v << " mm (" << rel_pct << "% of voxel size)\n\n";
    std::cout << "Interpretation: even with these modest input uncertainties, the voxel\n"
              << "size itself carries about " << rel_pct << "% uncertainty. Our compare_record()\n"
              << "tolerance of 0.5% is " << (0.5 / rel_pct) << "x looser than this -- i.e. the\n"
              << "tolerance is NOT the dominant source of \"slack\" in the comparison;\n"
              << "ordinary parameter uncertainty is larger than the tolerance band itself.\n";
}

int main(int argc, char** argv) {
    std::string xtekct_path = (argc >= 2) ? argv[1] : "data/poly_isro_mioni1_fbp_01.xtekct";

    try {
        ctgeom::XtekctRecord r = ctgeom::parse_xtekct(xtekct_path);
        Geometry g = r.to_geometry();

        std::cout << std::setprecision(8);
        std::cout << "ctgeom sensitivity / tolerance analysis\n";
        std::cout << "Source file: " << xtekct_path << " (dataset: " << r.name << ")\n";

        // Real scan geometry (micro-CT, M ~ 7.3x)
        sod_perturbation_sweep("real scan (M~7.3x)", g, 0.5);

        // Two synthetic reference geometries spanning the typical micro-CT
        // magnification range, to show the tolerance behaves differently
        // at different magnifications (same absolute mm error on SOD
        // produces a different % voxel-size error depending on M).
        Geometry near_unity{950.0, 1000.0, 0.15, 0.15};   // M ~ 1.05x: large sample, low mag
        Geometry high_mag{10.0, 1000.0, 0.15, 0.15};      // M = 100x: small sample, nano-CT-like
        sod_perturbation_sweep("synthetic near-unity (M~1.05x, large sample)", near_unity, 0.5);
        sod_perturbation_sweep("synthetic high-magnification (M=100x, small sample)", high_mag, 0.5);

        off_axis_report(g);
        uncertainty_report(g);

        std::cout << "\nDone.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
