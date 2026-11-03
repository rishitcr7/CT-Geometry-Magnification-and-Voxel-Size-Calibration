// Small CLI entry point: parse a .xtekct file (and, if present, its
// matching .ctinfo.xml report), recompute geometry, and print a
// calculated-vs-reported comparison plus a cross-check between the two
// independent Nikon report files. Exits non-zero if any field is
// flagged outside tolerance, so this is also usable in a test
// pipeline / CI check.
#include "ctgeom/geometry.hpp"
#include "ctgeom/xtekct_parser.hpp"
#include "ctgeom/ctinfo_parser.hpp"
#include "ctgeom/ctprofile_parser.hpp"
#include "ctgeom/compare.hpp"

#include <iostream>
#include <iomanip>
#include <fstream>

static bool file_exists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

static void print_field_comparison(const std::string& label, const ctgeom::FieldComparison& c) {
    std::cout << "--- " << label << " ---\n";
    std::cout << "value A    = " << c.calculated << "\n";
    std::cout << "value B    = " << c.reported << "\n";
    std::cout << "abs diff   = " << c.abs_diff << "\n";
    std::cout << "pct diff   = " << c.pct_diff << " %\n";
    std::cout << "status     = " << (c.flagged ? "FLAGGED (exceeds tolerance)" : "OK (within tolerance)") << "\n\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: ctgeom_cli <path-to-.xtekct> [path-to-.ctinfo.xml] [path-to-.ctprofile.xml]\n";
        std::cerr << "If the 2nd/3rd arguments are omitted, the CLI looks for\n";
        std::cerr << "<same-folder>/<dataset-name>_ctinfo.xml and _ctprofile.xml automatically.\n";
        return 2;
    }
    const std::string xtekct_path = argv[1];

    try {
        ctgeom::XtekctRecord r = ctgeom::parse_xtekct(xtekct_path);
        ctgeom::Geometry g = r.to_geometry();

        double M = ctgeom::magnification(g);
        double vx = ctgeom::voxel_size_x(g);
        double vy = ctgeom::voxel_size_y(g);

        std::cout << std::setprecision(10);
        std::cout << "File:                  " << xtekct_path << "\n";
        std::cout << "Dataset:               " << r.name << "\n";
        std::cout << "SOD (mm):              " << g.sod << "\n";
        std::cout << "SDD (mm):              " << g.sdd << "\n";
        std::cout << "Detector pixel (mm):   " << g.det_pixel_x << " x " << g.det_pixel_y << "\n";
        std::cout << "\n";
        std::cout << "Calculated magnification: " << M << "\n";
        std::cout << "Calculated voxel size X:  " << vx << " mm\n";
        std::cout << "Calculated voxel size Y:  " << vy << " mm\n";
        std::cout << "\n";
        std::cout << "Reported voxel size X (xtekct): " << r.voxel_size_x << " mm\n";
        std::cout << "Reported voxel size Y (xtekct): " << r.voxel_size_y << " mm\n";
        std::cout << "\n";

        auto report = ctgeom::compare_record(r);
        print_field_comparison("Comparison: calculated vs .xtekct-reported voxel_size_x", report.voxel_size_x);

        bool any_flagged = report.any_flagged;

        // --- Cross-check against .ctinfo.xml, if available ---
        std::string ctinfo_path = (argc >= 3) ? argv[2] : "";
        if (ctinfo_path.empty()) {
            // Auto-detect: Nikon names it "<Name>_ctinfo.xml" next to the .xtekct,
            // based on the folder this .xtekct file lives in.
            size_t slash = xtekct_path.find_last_of("/\\");
            std::string dir = (slash == std::string::npos) ? "" : xtekct_path.substr(0, slash + 1);
            std::string guess = dir + r.name + "_ctinfo.xml";
            if (file_exists(guess)) ctinfo_path = guess;
        }

        if (!ctinfo_path.empty() && file_exists(ctinfo_path)) {
            std::cout << "Found matching .ctinfo.xml: " << ctinfo_path << "\n\n";
            ctgeom::CtinfoRecord ci = ctgeom::parse_ctinfo(ctinfo_path);

            std::cout << "Identifier match (xtekct Name == ctinfo Identifier): "
                      << (r.name == ci.identifier ? "YES" : "NO -- these files may be from different scans!") << "\n\n";

            auto cross = ctgeom::cross_check(r, ci);
            print_field_comparison("Cross-check: SOD (.xtekct vs .ctinfo.xml)", cross.sod);
            print_field_comparison("Cross-check: SDD (.xtekct vs .ctinfo.xml)", cross.sdd);
            print_field_comparison("Cross-check: detector pixel size (.xtekct vs .ctinfo.xml)", cross.detector_pixel);
            print_field_comparison("Cross-check: our calculated magnification vs .ctinfo.xml-reported magnification", cross.magnification);

            any_flagged = any_flagged || cross.any_flagged || !cross.identifier_matches;
        } else {
            std::cout << "(No matching .ctinfo.xml found -- skipping cross-check. Pass it explicitly as a 2nd argument if you have one.)\n";
        }

        // --- Cross-check against .ctprofile.xml, if available ---
        std::string ctprofile_path = (argc >= 4) ? argv[3] : "";
        if (ctprofile_path.empty()) {
            size_t slash = xtekct_path.find_last_of("/\\");
            std::string dir = (slash == std::string::npos) ? "" : xtekct_path.substr(0, slash + 1);
            std::string guess = dir + r.name + "_ctprofile.xml";
            if (file_exists(guess)) ctprofile_path = guess;
        }

        if (!ctprofile_path.empty() && file_exists(ctprofile_path)) {
            std::cout << "Found matching .ctprofile.xml: " << ctprofile_path << "\n\n";
            ctgeom::CtprofileRecord cp = ctgeom::parse_ctprofile(ctprofile_path);

            std::cout << "VolumeOfInterest crop (pixel space): "
                      << "X[" << cp.voi.x_start << ".." << cp.voi.x_end << "] "
                      << "Y[" << cp.voi.y_start << ".." << cp.voi.y_end << "] "
                      << "Z[" << cp.voi.z_start << ".." << cp.voi.z_end << "]\n";

            auto voi = ctgeom::check_voi_consistency(r, cp);
            std::cout << "--- Cross-check: VolumeOfInterest crop vs .xtekct voxel dimensions ---\n";
            std::cout << "VoxelsX: crop implies " << voi.expected_voxels_x
                      << ", .xtekct reports " << voi.actual_voxels_x
                      << " -> " << (voi.voxels_x_match ? "MATCH" : "MISMATCH") << "\n";
            std::cout << "VoxelsY: crop implies " << voi.expected_voxels_y
                      << ", .xtekct reports " << voi.actual_voxels_y
                      << " -> " << (voi.voxels_y_match ? "MATCH" : "MISMATCH") << "\n";
            std::cout << "VoxelsZ: crop implies " << voi.expected_voxels_z
                      << ", .xtekct reports " << voi.actual_voxels_z
                      << " -> " << (voi.voxels_z_match ? "MATCH" : "MISMATCH") << "\n\n";

            any_flagged = any_flagged || !voi.all_match;
        } else {
            std::cout << "(No matching .ctprofile.xml found -- skipping VOI consistency check.)\n";
        }

        // --- Off-axis magnification uniformity (informational, not a flag) ---
        //
        // Unlike the checks above, a nonzero result here is NOT a sign that
        // anything is wrong with the files -- it's the expected cone-beam
        // geometry effect of assigning one global voxel size to a volume
        // whose true magnification varies with distance from the rotation
        // axis. It does not affect any_flagged / the exit code.
        {
            auto off_axis = ctgeom::check_off_axis_uniformity(r);
            std::cout << "--- Off-axis magnification uniformity (informational) ---\n";
            std::cout << "Field-of-view half-width used: " << off_axis.fov_half_width_mm << " mm\n";
            std::cout << "Axis magnification:             " << off_axis.magnification_axis << "\n";
            std::cout << "Magnification at FOV edge:      " << off_axis.magnification_at_edge << "\n";
            std::cout << "Difference from axis value:     " << off_axis.pct_diff << " %\n";
            if (off_axis.significant) {
                std::cout << "NOTE: this exceeds the +/-2% significance threshold. The single global\n"
                          << "voxel size used for this volume is a meaningfully worse approximation\n"
                          << "near the edge of the field of view than at its centre -- not a data\n"
                          << "error, just an inherent cone-beam effect worth knowing about for any\n"
                          << "precise measurement made away from the volume's centre.\n";
            } else {
                std::cout << "Within +/-2% -- the single global voxel size is a reasonable\n"
                          << "approximation across this volume's field of view.\n";
            }
            std::cout << "\n";
        }

        return any_flagged ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
