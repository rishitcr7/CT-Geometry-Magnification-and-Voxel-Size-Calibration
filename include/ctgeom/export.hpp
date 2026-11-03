// ctgeom::export
//
// Converts a ctgeom Geometry into the argument sets of two GPU
// reconstruction toolboxes. Pure arithmetic: nothing here runs, installs or
// links against ASTRA or TIGRE, and the outputs have not been fed to either
// toolbox.
//
// ASTRA (astra-toolbox.readthedocs.io, "3D Geometries"): by the page's
// convention the volume's voxel side is 1 and all projection-geometry
// lengths are in those units, so every millimetre length is divided by the
// voxel size.
// TIGRE (tigre.readthedocs.io, "Demo 01: Describing your geometry"):
// attributes are in millimetres, so values pass through unchanged.
#pragma once

#include "geometry.hpp"

#include <stdexcept>

namespace ctgeom {

struct AstraConeArgs {
    double det_spacing_x = 0.0;  // detector pixel pitch / voxel size
    double det_spacing_y = 0.0;
    int det_row_count = 0;
    int det_col_count = 0;
    double source_origin = 0.0;  // SOD / voxel size
    double origin_det = 0.0;     // (SDD - SOD) / voxel size
};

struct TigreGeometry {
    double DSD = 0.0;  // = SDD (mm)
    double DSO = 0.0;  // = SOD (mm)
    int nDetector_rows = 0;
    int nDetector_cols = 0;
    double dDetector_x = 0.0;  // mm
    double dDetector_y = 0.0;  // mm
    double dVoxel = 0.0;       // mm
};

inline AstraConeArgs to_astra_cone(const Geometry& g, double voxel_size_mm, int det_rows, int det_cols) {
    g.validate();
    if (voxel_size_mm <= 0.0) throw std::invalid_argument("to_astra_cone: voxel size must be > 0");
    if (det_rows <= 0 || det_cols <= 0) throw std::invalid_argument("to_astra_cone: detector size must be > 0");
    AstraConeArgs a;
    a.det_spacing_x = g.det_pixel_x / voxel_size_mm;
    a.det_spacing_y = g.det_pixel_y / voxel_size_mm;
    a.det_row_count = det_rows;
    a.det_col_count = det_cols;
    a.source_origin = g.sod / voxel_size_mm;
    a.origin_det = (g.sdd - g.sod) / voxel_size_mm;
    return a;
}

inline TigreGeometry to_tigre(const Geometry& g, double voxel_size_mm, int det_rows, int det_cols) {
    g.validate();
    if (voxel_size_mm <= 0.0) throw std::invalid_argument("to_tigre: voxel size must be > 0");
    if (det_rows <= 0 || det_cols <= 0) throw std::invalid_argument("to_tigre: detector size must be > 0");
    TigreGeometry t;
    t.DSD = g.sdd;
    t.DSO = g.sod;
    t.nDetector_rows = det_rows;
    t.nDetector_cols = det_cols;
    t.dDetector_x = g.det_pixel_x;
    t.dDetector_y = g.det_pixel_y;
    t.dVoxel = voxel_size_mm;
    return t;
}

} // namespace ctgeom
