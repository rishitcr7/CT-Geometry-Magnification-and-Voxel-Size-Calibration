// ctgeom::ctprofile_parser
//
// Parser for Nikon .ctprofile.xml files — the full RECONSTRUCTION
// SETTINGS file (as opposed to .ctinfo.xml, which is the ACQUISITION
// summary). This file is proper nested XML (unlike the flat
// .ctinfo.xml ScanInformation block), so we only extract the specific
// fields we need rather than attempting a general parse.
//
// The field this module cares about most is <VolumeOfInterest>: the
// pixel-space crop region (XStart/XEnd/YStart/YEnd/ZStart/ZEnd) that
// the reconstruction software applied to the full detector image
// before writing out the volume. This directly explains the VoxelsX/
// VoxelsY/VoxelsZ dimensions reported in the matching .xtekct file:
//   VoxelsX = XEnd - XStart + 1   (and similarly for Y, Z)
// If this doesn't hold, either the files are from different
// reconstruction jobs, or something in the crop/export pipeline
// changed between writing the two files — worth flagging either way.
#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <optional>

namespace ctgeom {

struct VolumeOfInterest {
    int x_start = 0, x_end = 0;
    int y_start = 0, y_end = 0;
    int z_start = 0, z_end = 0;

    int width()  const { return x_end - x_start + 1; }
    int height() const { return y_end - y_start + 1; }
    int depth()  const { return z_end - z_start + 1; }
};

struct CtprofileRecord {
    std::string profile_name;
    std::string job_guid;

    double detector_pixel_size_mm = 0.0; // <DetectorPixelSizeMM>
    double ct_axis_offset = 0.0;         // <CTAxisOffset>

    VolumeOfInterest voi;
    bool has_voi = false;

    int projections = 0;
    bool manual_centre_of_rotation = false;
};

namespace detail_ctprofile {
    inline std::optional<std::string> extract_tag(const std::string& xml, const std::string& tag_name,
                                                    size_t from_pos = 0) {
        const std::string open_tag = "<" + tag_name + ">";
        const std::string close_tag = "</" + tag_name + ">";
        size_t start = xml.find(open_tag, from_pos);
        if (start == std::string::npos) return std::nullopt;
        start += open_tag.size();
        size_t end = xml.find(close_tag, start);
        if (end == std::string::npos) return std::nullopt;
        return xml.substr(start, end - start);
    }

    inline double to_double(const std::string& s, const std::string& field_name) {
        try { return std::stod(s); }
        catch (...) { throw std::runtime_error("ctprofile_parser: field '" + field_name + "' is not numeric: '" + s + "'"); }
    }

    inline int to_int(const std::string& s, const std::string& field_name) {
        return static_cast<int>(to_double(s, field_name));
    }
}

// Parses a .ctprofile.xml file. Throws std::runtime_error if the
// <VolumeOfInterest> block or its six required fields are missing —
// everything else on this struct is best-effort (defaults to empty/0
// if absent), since only VOI is load-bearing for the cross-check this
// module exists to support.
inline CtprofileRecord parse_ctprofile(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("ctprofile_parser: could not open file: " + path);

    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string xml = buffer.str();

    CtprofileRecord r;

    if (auto v = detail_ctprofile::extract_tag(xml, "ProfileName")) r.profile_name = *v;
    if (auto v = detail_ctprofile::extract_tag(xml, "JobGuid")) r.job_guid = *v;
    if (auto v = detail_ctprofile::extract_tag(xml, "DetectorPixelSizeMM"))
        r.detector_pixel_size_mm = detail_ctprofile::to_double(*v, "DetectorPixelSizeMM");
    if (auto v = detail_ctprofile::extract_tag(xml, "CTAxisOffset"))
        r.ct_axis_offset = detail_ctprofile::to_double(*v, "CTAxisOffset");
    if (auto v = detail_ctprofile::extract_tag(xml, "Projections"))
        r.projections = detail_ctprofile::to_int(*v, "Projections");
    if (auto v = detail_ctprofile::extract_tag(xml, "ManualCentreOfRotation"))
        r.manual_centre_of_rotation = (*v == "true");

    auto voi_start = xml.find("<VolumeOfInterest>");
    auto voi_end = xml.find("</VolumeOfInterest>");
    if (voi_start == std::string::npos || voi_end == std::string::npos)
        throw std::runtime_error("ctprofile_parser: <VolumeOfInterest> section not found in " + path);
    const std::string voi_section = xml.substr(voi_start, voi_end - voi_start);

    auto require_int = [&](const char* tag) -> int {
        auto v = detail_ctprofile::extract_tag(voi_section, tag);
        if (!v) throw std::runtime_error(std::string("ctprofile_parser: missing required VolumeOfInterest field '") + tag + "'");
        return detail_ctprofile::to_int(*v, tag);
    };

    r.voi.x_start = require_int("XStart");
    r.voi.x_end   = require_int("XEnd");
    r.voi.y_start = require_int("YStart");
    r.voi.y_end   = require_int("YEnd");
    r.voi.z_start = require_int("ZStart");
    r.voi.z_end   = require_int("ZEnd");
    r.has_voi = true;

    return r;
}

} // namespace ctgeom
