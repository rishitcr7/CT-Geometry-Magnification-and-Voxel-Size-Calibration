// pybind11 bindings exposing ctgeom's geometry, parser and compare
// functionality to Python for interactive exploration / prototyping,
// per Section 10 of the Plan of Action.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include "ctgeom/geometry.hpp"
#include "ctgeom/xtekct_parser.hpp"
#include "ctgeom/ctinfo_parser.hpp"
#include "ctgeom/ctprofile_parser.hpp"
#include "ctgeom/compare.hpp"
#include "ctgeom/calibration.hpp"
#include "ctgeom/image_edge.hpp"
#include "ctgeom/export.hpp"

namespace py = pybind11;

PYBIND11_MODULE(ctgeom, m) {
    m.doc() = "CT cone-beam geometry, magnification and voxel-size calibration";

    py::class_<ctgeom::Geometry>(m, "Geometry")
        .def(py::init<>())
        .def(py::init<double, double, double, double>(),
             py::arg("sod"), py::arg("sdd"), py::arg("det_pixel_x"), py::arg("det_pixel_y"))
        .def_readwrite("sod", &ctgeom::Geometry::sod)
        .def_readwrite("sdd", &ctgeom::Geometry::sdd)
        .def_readwrite("det_pixel_x", &ctgeom::Geometry::det_pixel_x)
        .def_readwrite("det_pixel_y", &ctgeom::Geometry::det_pixel_y)
        .def("validate", &ctgeom::Geometry::validate)
        .def("__repr__", [](const ctgeom::Geometry& g) {
            return "<Geometry sod=" + std::to_string(g.sod) +
                   " sdd=" + std::to_string(g.sdd) +
                   " det_pixel_x=" + std::to_string(g.det_pixel_x) +
                   " det_pixel_y=" + std::to_string(g.det_pixel_y) + ">";
        });

    m.def("magnification", &ctgeom::magnification, py::arg("geometry"),
          "Geometric magnification at the rotation axis: M = SDD / SOD");
    m.def("voxel_size_x", &ctgeom::voxel_size_x, py::arg("geometry"),
          "Effective object-side voxel size along X: det_pixel_x / M");
    m.def("voxel_size_y", &ctgeom::voxel_size_y, py::arg("geometry"),
          "Effective object-side voxel size along Y: det_pixel_y / M");
    m.def("magnification_off_axis", &ctgeom::magnification_off_axis,
          py::arg("geometry"), py::arg("delta_from_axis"),
          "Magnification of a feature offset from the rotation axis along the beam direction");

    py::class_<ctgeom::VoxelSizeUncertaintyInputs>(m, "VoxelSizeUncertaintyInputs")
        .def(py::init<>())
        .def(py::init<double, double, double>(),
             py::arg("sigma_sod"), py::arg("sigma_sdd"), py::arg("sigma_pixel"))
        .def_readwrite("sigma_sod", &ctgeom::VoxelSizeUncertaintyInputs::sigma_sod)
        .def_readwrite("sigma_sdd", &ctgeom::VoxelSizeUncertaintyInputs::sigma_sdd)
        .def_readwrite("sigma_pixel", &ctgeom::VoxelSizeUncertaintyInputs::sigma_pixel);

    m.def("voxel_size_sigma_x", &ctgeom::voxel_size_sigma_x,
          py::arg("geometry"), py::arg("uncertainty_inputs"),
          "First-order propagated 1-sigma uncertainty in voxel_size_x");

    py::class_<ctgeom::XtekctRecord>(m, "XtekctRecord")
        .def(py::init<>())
        .def_readonly("voxels_x", &ctgeom::XtekctRecord::voxels_x)
        .def_readonly("voxels_y", &ctgeom::XtekctRecord::voxels_y)
        .def_readonly("voxels_z", &ctgeom::XtekctRecord::voxels_z)
        .def_readonly("voxel_size_x", &ctgeom::XtekctRecord::voxel_size_x)
        .def_readonly("voxel_size_y", &ctgeom::XtekctRecord::voxel_size_y)
        .def_readonly("voxel_size_z", &ctgeom::XtekctRecord::voxel_size_z)
        .def_readonly("src_to_object", &ctgeom::XtekctRecord::src_to_object)
        .def_readonly("src_to_detector", &ctgeom::XtekctRecord::src_to_detector)
        .def_readonly("detector_pixels_x", &ctgeom::XtekctRecord::detector_pixels_x)
        .def_readonly("detector_pixels_y", &ctgeom::XtekctRecord::detector_pixels_y)
        .def_readonly("detector_pixel_size_x", &ctgeom::XtekctRecord::detector_pixel_size_x)
        .def_readonly("detector_pixel_size_y", &ctgeom::XtekctRecord::detector_pixel_size_y)
        .def_readonly("units", &ctgeom::XtekctRecord::units)
        .def_readonly("projections", &ctgeom::XtekctRecord::projections)
        .def_readonly("name", &ctgeom::XtekctRecord::name)
        .def_readonly("system_serial_number", &ctgeom::XtekctRecord::system_serial_number)
        .def("to_geometry", &ctgeom::XtekctRecord::to_geometry);

    m.def("parse_xtekct", &ctgeom::parse_xtekct, py::arg("path"),
          "Parse a Nikon .xtekct file into an XtekctRecord");

    py::class_<ctgeom::FieldComparison>(m, "FieldComparison")
        .def_readonly("field_name", &ctgeom::FieldComparison::field_name)
        .def_readonly("calculated", &ctgeom::FieldComparison::calculated)
        .def_readonly("reported", &ctgeom::FieldComparison::reported)
        .def_readonly("abs_diff", &ctgeom::FieldComparison::abs_diff)
        .def_readonly("pct_diff", &ctgeom::FieldComparison::pct_diff)
        .def_readonly("flagged", &ctgeom::FieldComparison::flagged);

    py::class_<ctgeom::ComparisonReport>(m, "ComparisonReport")
        .def_readonly("voxel_size_x", &ctgeom::ComparisonReport::voxel_size_x)
        .def_readonly("any_flagged", &ctgeom::ComparisonReport::any_flagged);

    m.def("compare_record", &ctgeom::compare_record,
          py::arg("record"), py::arg("tolerance_pct") = 0.5,
          "Compare calculated vs Nikon-reported voxel size for a parsed XtekctRecord");

    py::class_<ctgeom::CtinfoRecord>(m, "CtinfoRecord")
        .def(py::init<>())
        .def_readonly("identifier", &ctgeom::CtinfoRecord::identifier)
        .def_readonly("system_type", &ctgeom::CtinfoRecord::system_type)
        .def_readonly("acquisition_software", &ctgeom::CtinfoRecord::acquisition_software)
        .def_readonly("tube_voltage_kv", &ctgeom::CtinfoRecord::tube_voltage_kv)
        .def_readonly("tube_current_ua", &ctgeom::CtinfoRecord::tube_current_ua)
        .def_readonly("projections", &ctgeom::CtinfoRecord::projections)
        .def_readonly("detector_pixel_size_um", &ctgeom::CtinfoRecord::detector_pixel_size_um)
        .def_readonly("magnification", &ctgeom::CtinfoRecord::magnification)
        .def_readonly("source_to_object_mm", &ctgeom::CtinfoRecord::source_to_object_mm)
        .def_readonly("source_to_detector_mm", &ctgeom::CtinfoRecord::source_to_detector_mm)
        .def("detector_pixel_size_mm", &ctgeom::CtinfoRecord::detector_pixel_size_mm);

    m.def("parse_ctinfo", &ctgeom::parse_ctinfo, py::arg("path"),
          "Parse a Nikon .ctinfo.xml report into a CtinfoRecord");

    py::class_<ctgeom::CrossCheckReport>(m, "CrossCheckReport")
        .def_readonly("sod", &ctgeom::CrossCheckReport::sod)
        .def_readonly("sdd", &ctgeom::CrossCheckReport::sdd)
        .def_readonly("detector_pixel", &ctgeom::CrossCheckReport::detector_pixel)
        .def_readonly("magnification", &ctgeom::CrossCheckReport::magnification)
        .def_readonly("identifier_matches", &ctgeom::CrossCheckReport::identifier_matches)
        .def_readonly("any_flagged", &ctgeom::CrossCheckReport::any_flagged);

    m.def("cross_check", &ctgeom::cross_check,
          py::arg("xtekct_record"), py::arg("ctinfo_record"), py::arg("tolerance_pct") = 0.01,
          "Cross-check a .xtekct record against an independent .ctinfo.xml record from the same scan");

    py::class_<ctgeom::VolumeOfInterest>(m, "VolumeOfInterest")
        .def(py::init<>())
        .def_readwrite("x_start", &ctgeom::VolumeOfInterest::x_start)
        .def_readwrite("x_end", &ctgeom::VolumeOfInterest::x_end)
        .def_readwrite("y_start", &ctgeom::VolumeOfInterest::y_start)
        .def_readwrite("y_end", &ctgeom::VolumeOfInterest::y_end)
        .def_readwrite("z_start", &ctgeom::VolumeOfInterest::z_start)
        .def_readwrite("z_end", &ctgeom::VolumeOfInterest::z_end)
        .def("width", &ctgeom::VolumeOfInterest::width)
        .def("height", &ctgeom::VolumeOfInterest::height)
        .def("depth", &ctgeom::VolumeOfInterest::depth);

    py::class_<ctgeom::CtprofileRecord>(m, "CtprofileRecord")
        .def(py::init<>())
        .def_readonly("profile_name", &ctgeom::CtprofileRecord::profile_name)
        .def_readonly("job_guid", &ctgeom::CtprofileRecord::job_guid)
        .def_readonly("detector_pixel_size_mm", &ctgeom::CtprofileRecord::detector_pixel_size_mm)
        .def_readonly("ct_axis_offset", &ctgeom::CtprofileRecord::ct_axis_offset)
        .def_readonly("voi", &ctgeom::CtprofileRecord::voi)
        .def_readonly("has_voi", &ctgeom::CtprofileRecord::has_voi)
        .def_readonly("projections", &ctgeom::CtprofileRecord::projections)
        .def_readonly("manual_centre_of_rotation", &ctgeom::CtprofileRecord::manual_centre_of_rotation);

    m.def("parse_ctprofile", &ctgeom::parse_ctprofile, py::arg("path"),
          "Parse a Nikon .ctprofile.xml reconstruction-settings file into a CtprofileRecord");

    py::class_<ctgeom::VoiConsistencyReport>(m, "VoiConsistencyReport")
        .def_readonly("voxels_x_match", &ctgeom::VoiConsistencyReport::voxels_x_match)
        .def_readonly("voxels_y_match", &ctgeom::VoiConsistencyReport::voxels_y_match)
        .def_readonly("voxels_z_match", &ctgeom::VoiConsistencyReport::voxels_z_match)
        .def_readonly("expected_voxels_x", &ctgeom::VoiConsistencyReport::expected_voxels_x)
        .def_readonly("actual_voxels_x", &ctgeom::VoiConsistencyReport::actual_voxels_x)
        .def_readonly("expected_voxels_y", &ctgeom::VoiConsistencyReport::expected_voxels_y)
        .def_readonly("actual_voxels_y", &ctgeom::VoiConsistencyReport::actual_voxels_y)
        .def_readonly("expected_voxels_z", &ctgeom::VoiConsistencyReport::expected_voxels_z)
        .def_readonly("actual_voxels_z", &ctgeom::VoiConsistencyReport::actual_voxels_z)
        .def_readonly("all_match", &ctgeom::VoiConsistencyReport::all_match);

    m.def("check_voi_consistency", &ctgeom::check_voi_consistency,
          py::arg("xtekct_record"), py::arg("ctprofile_record"),
          "Check that .ctprofile.xml's VolumeOfInterest crop exactly explains .xtekct's voxel dimensions");

    py::class_<ctgeom::OffAxisUniformityReport>(m, "OffAxisUniformityReport")
        .def_readonly("fov_half_width_mm", &ctgeom::OffAxisUniformityReport::fov_half_width_mm)
        .def_readonly("magnification_axis", &ctgeom::OffAxisUniformityReport::magnification_axis)
        .def_readonly("magnification_at_edge", &ctgeom::OffAxisUniformityReport::magnification_at_edge)
        .def_readonly("pct_diff", &ctgeom::OffAxisUniformityReport::pct_diff)
        .def_readonly("significant", &ctgeom::OffAxisUniformityReport::significant);

    m.def("check_off_axis_uniformity", &ctgeom::check_off_axis_uniformity,
          py::arg("xtekct_record"), py::arg("significance_threshold_pct") = 2.0,
          "Informational check: how much magnification (and therefore the single-voxel-size "
          "assumption) varies between the volume's centre and the edge of its field of view. "
          "A significant result is expected cone-beam geometry, not a data error.");

    // --- Reference-sphere calibration (synthetic-data tested only) ---
    py::class_<ctgeom::Point2D>(m, "Point2D")
        .def(py::init<>())
        .def(py::init([](double x, double y) { return ctgeom::Point2D{x, y}; }), py::arg("x"), py::arg("y"))
        .def_readwrite("x", &ctgeom::Point2D::x)
        .def_readwrite("y", &ctgeom::Point2D::y);

    py::class_<ctgeom::CircleFit>(m, "CircleFit")
        .def_readonly("cx", &ctgeom::CircleFit::cx)
        .def_readonly("cy", &ctgeom::CircleFit::cy)
        .def_readonly("radius", &ctgeom::CircleFit::radius)
        .def_readonly("rms_residual", &ctgeom::CircleFit::rms_residual)
        .def("diameter", &ctgeom::CircleFit::diameter);
    m.def("fit_circle", &ctgeom::fit_circle, py::arg("points"),
          "Least-squares circle fit to silhouette edge points");

    py::class_<ctgeom::LineFit>(m, "LineFit")
        .def_readonly("slope", &ctgeom::LineFit::slope)
        .def_readonly("intercept", &ctgeom::LineFit::intercept)
        .def_readonly("rms_residual", &ctgeom::LineFit::rms_residual)
        .def_readonly("slope_stderr", &ctgeom::LineFit::slope_stderr)
        .def_readonly("intercept_stderr", &ctgeom::LineFit::intercept_stderr)
        .def_readonly("n", &ctgeom::LineFit::n);
    m.def("fit_line", &ctgeom::fit_line, py::arg("x"), py::arg("y"));

    m.def("voxel_size_from_sphere", &ctgeom::voxel_size_from_sphere,
          py::arg("true_diameter_mm"), py::arg("diameter_pixels"));

    py::class_<ctgeom::CalibrationResult>(m, "CalibrationResult")
        .def_readonly("line", &ctgeom::CalibrationResult::line)
        .def_readonly("implied_sdd", &ctgeom::CalibrationResult::implied_sdd)
        .def_readonly("implied_sod_at_zero", &ctgeom::CalibrationResult::implied_sod_at_zero)
        .def_readonly("detector_pixel", &ctgeom::CalibrationResult::detector_pixel)
        .def("voxel_size_at", &ctgeom::CalibrationResult::voxel_size_at, py::arg("stage_position_mm"));

    m.def("calibrate_from_diameters", &ctgeom::calibrate_from_diameters,
          py::arg("stage_positions_mm"), py::arg("diameters_px"),
          py::arg("true_diameter_mm"), py::arg("detector_pixel_mm"),
          "Fit voxel size vs stage position from per-position sphere diameters (pixels)");
    m.def("calibrate_from_edges", &ctgeom::calibrate_from_edges,
          py::arg("stage_positions_mm"), py::arg("edge_points_per_position"),
          py::arg("true_diameter_mm"), py::arg("detector_pixel_mm"));

    py::class_<ctgeom::CalibrationCheck>(m, "CalibrationCheck")
        .def_readonly("sdd_pct_diff", &ctgeom::CalibrationCheck::sdd_pct_diff)
        .def_readonly("sod_pct_diff", &ctgeom::CalibrationCheck::sod_pct_diff)
        .def_readonly("voxel_size_pct_diff", &ctgeom::CalibrationCheck::voxel_size_pct_diff)
        .def_readonly("flagged", &ctgeom::CalibrationCheck::flagged);
    m.def("compare_calibration_to_geometry", &ctgeom::compare_calibration_to_geometry,
          py::arg("calibration"), py::arg("nominal_geometry"), py::arg("tolerance_pct") = 0.5);

    m.def("simulate_sphere_edge", &ctgeom::simulate_sphere_edge,
          py::arg("geometry"), py::arg("stage_position_mm"), py::arg("true_diameter_mm"),
          py::arg("n_points"), py::arg("noise_sigma_px"), py::arg("seed"),
          py::arg("centre_x_px") = 1000.0, py::arg("centre_y_px") = 1000.0,
          "Synthetic sphere silhouette edge points, for testing the calibration code");

    m.def("scaled_tolerance_pct", &ctgeom::scaled_tolerance_pct,
          py::arg("geometry"), py::arg("sigma_pos_mm") = 0.05, py::arg("k_sigma") = 3.0,
          py::arg("base_pct") = 0.5,
          "Opt-in magnification-aware tolerance (sigma_pos default is an illustrative placeholder)");

    // --- Sphere edge finder (synthetic-image tested only) ---
    m.def("find_sphere_edge_points",
          [](py::array_t<float, py::array::c_style | py::array::forcecast> image,
             double approx_cx, double approx_cy, double max_radius_px, size_t n_rays) {
              if (image.ndim() != 2) throw std::invalid_argument("image must be a 2D array");
              ctgeom::Image im(static_cast<size_t>(image.shape(1)), static_cast<size_t>(image.shape(0)));
              std::copy(image.data(), image.data() + image.size(), im.data.begin());
              ctgeom::EdgeFinderOptions opt;
              opt.n_rays = n_rays;
              return ctgeom::find_sphere_edge_points(im, approx_cx, approx_cy, max_radius_px, opt);
          },
          py::arg("image"), py::arg("approx_cx"), py::arg("approx_cy"), py::arg("max_radius_px"),
          py::arg("n_rays") = 360,
          "Sub-pixel sphere silhouette edge points from a 2D image (rows = y, columns = x). "
          "Tested on synthetic images only.");

    // --- Export to GPU toolbox argument sets ---
    py::class_<ctgeom::AstraConeArgs>(m, "AstraConeArgs")
        .def_readonly("det_spacing_x", &ctgeom::AstraConeArgs::det_spacing_x)
        .def_readonly("det_spacing_y", &ctgeom::AstraConeArgs::det_spacing_y)
        .def_readonly("det_row_count", &ctgeom::AstraConeArgs::det_row_count)
        .def_readonly("det_col_count", &ctgeom::AstraConeArgs::det_col_count)
        .def_readonly("source_origin", &ctgeom::AstraConeArgs::source_origin)
        .def_readonly("origin_det", &ctgeom::AstraConeArgs::origin_det);
    m.def("to_astra_cone", &ctgeom::to_astra_cone,
          py::arg("geometry"), py::arg("voxel_size_mm"), py::arg("det_rows"), py::arg("det_cols"),
          "Arguments for astra_create_proj_geom('cone', ...) in voxel units (not run against ASTRA)");

    py::class_<ctgeom::TigreGeometry>(m, "TigreGeometry")
        .def_readonly("DSD", &ctgeom::TigreGeometry::DSD)
        .def_readonly("DSO", &ctgeom::TigreGeometry::DSO)
        .def_readonly("nDetector_rows", &ctgeom::TigreGeometry::nDetector_rows)
        .def_readonly("nDetector_cols", &ctgeom::TigreGeometry::nDetector_cols)
        .def_readonly("dDetector_x", &ctgeom::TigreGeometry::dDetector_x)
        .def_readonly("dDetector_y", &ctgeom::TigreGeometry::dDetector_y)
        .def_readonly("dVoxel", &ctgeom::TigreGeometry::dVoxel);
    m.def("to_tigre", &ctgeom::to_tigre,
          py::arg("geometry"), py::arg("voxel_size_mm"), py::arg("det_rows"), py::arg("det_cols"),
          "TIGRE geometry attributes in mm (not run against TIGRE)");
}
