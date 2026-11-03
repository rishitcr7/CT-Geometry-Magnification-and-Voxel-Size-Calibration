// ctgeom::ctinfo_parser
//
// Parser for Nikon .ctinfo.xml reports — the human-readable scan summary
// that sits alongside the .xtekct file. This is a SECOND, INDEPENDENT
// source of the same geometry numbers (SOD, SDD, detector pixel size,
// magnification), produced by Nikon's own acquisition software rather
// than its reconstruction software. Parsing both and comparing them is
// a cross-check: if .xtekct and .ctinfo.xml ever disagree, that itself
// is a useful signal (see ctgeom::cross_check below), independent of
// whether either one agrees with our own calculation.
//
// This is NOT a general-purpose XML parser. The .ctinfo.xml format is
// simple, flat, single-line-per-tag markup with no attributes-as-data,
// no nested repeats we need, and no CDATA/entities to worry about in
// the fields we read. A small tag-scoped substring extractor is
// sufficient and avoids pulling in a full XML library for one file
// format. If Nikon's software ever emits a differently-shaped report
// (multi-line values, nested ScanInformation, etc.) this parser should
// be revisited rather than trusted blindly.
#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <optional>

namespace ctgeom {

struct CtinfoRecord {
    std::string identifier;         // dataset name
    std::string system_type;        // e.g. "XT H 225 ST 2x"
    std::string acquisition_software;

    double tube_voltage_kv = 0.0;
    double tube_current_ua = 0.0;
    int projections = 0;
    double detector_pixel_size_um = 0.0; // micrometers (NOTE: different unit from .xtekct's mm!)

    double magnification = 0.0;      // as computed by Nikon's acquisition software
    double source_to_object_mm = 0.0;
    double source_to_detector_mm = 0.0;

    // Convenience: detector pixel size converted to mm, so it's directly
    // comparable with the .xtekct record's DetectorPixelSizeX (which is
    // already in mm).
    double detector_pixel_size_mm() const { return detector_pixel_size_um / 1000.0; }
};

namespace detail_xml {
    // Finds the first <tagName>...</tagName> (or self-closing <tagName/>)
    // after `from_pos` and returns its inner text, or std::nullopt if the
    // tag isn't present / is self-closing (empty).
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
        try {
            return std::stod(s);
        } catch (...) {
            throw std::runtime_error("ctinfo_parser: field '" + field_name + "' is not numeric: '" + s + "'");
        }
    }
}

// Parses a .ctinfo.xml file. Throws std::runtime_error if a required
// geometry field (the ones needed for magnification/voxel-size
// cross-checking) is missing, same policy as the .xtekct parser: a
// missing field is a data problem worth surfacing, not silently
// defaulting to 0.
inline CtinfoRecord parse_ctinfo(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("ctinfo_parser: could not open file: " + path);

    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string xml = buffer.str();

    // Scope the search to <ScanInformation>...</ScanInformation> so we
    // don't accidentally match a same-named tag elsewhere in the file
    // (there isn't one today, but this keeps the parser honest about
    // which section it's reading from).
    auto scan_start = xml.find("<ScanInformation>");
    auto scan_end = xml.find("</ScanInformation>");
    if (scan_start == std::string::npos || scan_end == std::string::npos)
        throw std::runtime_error("ctinfo_parser: <ScanInformation> section not found in " + path);
    const std::string scan_section = xml.substr(scan_start, scan_end - scan_start);

    CtinfoRecord r;

    if (auto v = detail_xml::extract_tag(xml, "Identifier")) r.identifier = *v;
    if (auto v = detail_xml::extract_tag(xml, "SystemType")) r.system_type = *v;
    if (auto v = detail_xml::extract_tag(xml, "AcquisitionSoftware")) r.acquisition_software = *v;

    auto require = [&](const char* tag) -> std::string {
        auto v = detail_xml::extract_tag(scan_section, tag);
        if (!v) throw std::runtime_error(std::string("ctinfo_parser: missing required field '") + tag + "'");
        return *v;
    };

    r.tube_voltage_kv = detail_xml::to_double(require("TubeVoltageInkV"), "TubeVoltageInkV");
    r.tube_current_ua = detail_xml::to_double(require("TubeCurrentInuA"), "TubeCurrentInuA");

    if (auto v = detail_xml::extract_tag(scan_section, "Projections"))
        r.projections = static_cast<int>(detail_xml::to_double(*v, "Projections"));

    r.detector_pixel_size_um = detail_xml::to_double(require("DetectorPixelSizeMicrometers"), "DetectorPixelSizeMicrometers");
    r.magnification = detail_xml::to_double(require("Magnification"), "Magnification");
    r.source_to_object_mm = detail_xml::to_double(require("SourceToObjectMM"), "SourceToObjectMM");
    r.source_to_detector_mm = detail_xml::to_double(require("SourceToDetectorMM"), "SourceToDetectorMM");

    return r;
}

} // namespace ctgeom
