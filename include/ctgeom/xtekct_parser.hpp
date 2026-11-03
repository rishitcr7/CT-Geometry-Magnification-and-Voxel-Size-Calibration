// ctgeom::xtekct_parser
//
// Parser for Nikon/X-Tek .xtekct acquisition+reconstruction files.
// Format is a flat INI-style file: [Section] headers followed by
// Key=Value lines. We only need the [XTekCT] section for geometry.
//
// This is intentionally a small, dependency-free parser (no XML/HTML
// library needed for this format) that reads the whole file into a
// string->string map, then type-converts the fields we care about.
#pragma once

#include "geometry.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace ctgeom {

// Everything we extract from a .xtekct file that is relevant to
// geometry/voxel-size calculation and to validating it against the
// values the Nikon reconstruction software itself computed.
struct XtekctRecord {
    // Reconstruction volume
    int voxels_x = 0, voxels_y = 0, voxels_z = 0;
    double voxel_size_x = 0.0, voxel_size_y = 0.0, voxel_size_z = 0.0; // as reported by Nikon

    // Acquisition geometry
    double src_to_object = 0.0;
    double src_to_detector = 0.0;

    // Detector
    int detector_pixels_x = 0, detector_pixels_y = 0;
    double detector_pixel_size_x = 0.0, detector_pixel_size_y = 0.0;

    // Misc useful metadata
    std::string units;          // e.g. "mm"
    int projections = 0;
    std::string name;
    std::string system_serial_number;

    // Build the minimal Geometry struct ctgeom::geometry functions need.
    Geometry to_geometry() const {
        Geometry g;
        g.sod = src_to_object;
        g.sdd = src_to_detector;
        g.det_pixel_x = detector_pixel_size_x;
        g.det_pixel_y = detector_pixel_size_y;
        return g;
    }
};

namespace detail {
    inline std::string trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }
}

// Parses a .xtekct file into a flat key->value map (values as strings).
// Only the [XTekCT] section's keys are kept (that's where every geometry
// field lives); other sections ([Xrays], [CTPro], ...) are skipped since
// they're not needed for geometry/voxel-size work, but this is easy to
// extend if a later task needs them.
inline std::unordered_map<std::string, std::string> parse_xtekct_raw(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("xtekct_parser: could not open file: " + path);

    std::unordered_map<std::string, std::string> kv;
    std::string line;
    bool in_target_section = false;

    while (std::getline(in, line)) {
        std::string trimmed = detail::trim(line);
        if (trimmed.empty()) continue;

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            in_target_section = (trimmed == "[XTekCT]");
            continue;
        }
        if (!in_target_section) continue;

        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue; // malformed/blank-value line, skip
        std::string key = detail::trim(trimmed.substr(0, eq));
        std::string value = detail::trim(trimmed.substr(eq + 1));
        kv[key] = value;
    }
    return kv;
}

inline double get_double(const std::unordered_map<std::string, std::string>& kv,
                          const std::string& key, bool required = true) {
    auto it = kv.find(key);
    if (it == kv.end()) {
        if (required) throw std::runtime_error("xtekct_parser: missing required field '" + key + "'");
        return 0.0;
    }
    try {
        return std::stod(it->second);
    } catch (...) {
        throw std::runtime_error("xtekct_parser: field '" + key + "' is not numeric: '" + it->second + "'");
    }
}

inline int get_int(const std::unordered_map<std::string, std::string>& kv,
                    const std::string& key, bool required = true) {
    return static_cast<int>(get_double(kv, key, required));
}

inline std::string get_string(const std::unordered_map<std::string, std::string>& kv,
                               const std::string& key, const std::string& fallback = "") {
    auto it = kv.find(key);
    return it == kv.end() ? fallback : it->second;
}

// Parses a .xtekct file and returns a typed XtekctRecord.
// Throws std::runtime_error if a required geometry field is missing or
// non-numeric, rather than silently defaulting — a missing field is a
// data problem the caller should know about, not something to paper over.
inline XtekctRecord parse_xtekct(const std::string& path) {
    auto kv = parse_xtekct_raw(path);

    XtekctRecord r;
    r.voxels_x = get_int(kv, "VoxelsX");
    r.voxels_y = get_int(kv, "VoxelsY");
    r.voxels_z = get_int(kv, "VoxelsZ");
    r.voxel_size_x = get_double(kv, "VoxelSizeX");
    r.voxel_size_y = get_double(kv, "VoxelSizeY");
    r.voxel_size_z = get_double(kv, "VoxelSizeZ");

    r.src_to_object = get_double(kv, "SrcToObject");
    r.src_to_detector = get_double(kv, "SrcToDetector");

    r.detector_pixels_x = get_int(kv, "DetectorPixelsX");
    r.detector_pixels_y = get_int(kv, "DetectorPixelsY");
    r.detector_pixel_size_x = get_double(kv, "DetectorPixelSizeX");
    r.detector_pixel_size_y = get_double(kv, "DetectorPixelSizeY");

    r.units = get_string(kv, "Units", "mm");
    r.projections = get_int(kv, "Projections", /*required=*/false);
    r.name = get_string(kv, "Name");
    r.system_serial_number = get_string(kv, "SystemSerialNumber");

    return r;
}

} // namespace ctgeom
