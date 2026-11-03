#!/usr/bin/env python3
"""
Sensitivity sweep: how much does mechanical positioning uncertainty
affect computed voxel size, across the magnification range the scanner
in this project (Nikon XT H 225 ST 2x) actually supports (~1.5x-300x,
per the system's published specification)?

This does NOT require a new real scan. It's a synthetic study built on
top of the already-validated geometry module (ctgeom.magnification,
ctgeom.voxel_size_x, ctgeom.magnification_off_axis,
ctgeom.voxel_size_sigma_x), using realistic stage-positioning precision
figures as the uncertainty input. The real scan we DO have
(poly_isro_mioni1_fbp, M=7.35x) is included as one point in the sweep
so the synthetic results stay anchored to a value we've already
cross-checked against three independent real files.

Output: analysis/sensitivity_sweep.csv (one row per magnification
tested) and a plain-language summary printed to stdout.
"""
import ctgeom
import csv
import os

# Fixed detector pixel pitch, taken from the real scan (PerkinElmer
# XRD4343CT panel) -- same detector, we're only varying geometry.
DET_PIXEL_MM = 0.15

# Hold SDD fixed at the real scan's value (the detector's physical
# travel is more constrained than the object's position on most
# systems) and sweep SOD to cover the scanner's full magnification
# range, 1.5x to 300x, per Nikon's published spec for this system.
SDD_MM = 1123.1419677734375

# Realistic object-positioning uncertainties to test, in mm. These
# represent plausible real-world sources of error: rotary stage
# runout, manual sample mounting repeatability, thermal drift during
# a scan. Values are illustrative engineering estimates (documented as
# such), not taken from a manufacturer spec sheet -- a real stage
# repeatability figure for this specific system is an open item to
# get from the Nikon XT H 225 ST documentation or from the advisor
# before treating these as authoritative.
POSITION_UNCERTAINTIES_MM = [0.005, 0.02, 0.05]

# Magnifications to sweep across the scanner's supported range.
MAGNIFICATIONS = [1.5, 2, 5, 7.345523741059397, 10, 20, 50, 100, 150, 200, 300]


def sod_for_magnification(M: float, sdd: float) -> float:
    """Invert M = SDD/SOD to get the SOD that gives a target magnification."""
    return sdd / M


def main():
    rows = []
    for M_target in MAGNIFICATIONS:
        sod = sod_for_magnification(M_target, SDD_MM)
        g = ctgeom.Geometry(sod=sod, sdd=SDD_MM, det_pixel_x=DET_PIXEL_MM, det_pixel_y=DET_PIXEL_MM)

        M_actual = ctgeom.magnification(g)
        voxel = ctgeom.voxel_size_x(g)

        row = {
            "magnification": round(M_actual, 4),
            "sod_mm": round(sod, 6),
            "sdd_mm": SDD_MM,
            "voxel_size_mm": voxel,
            "voxel_size_um": voxel * 1000.0,
        }

        for sigma_sod in POSITION_UNCERTAINTIES_MM:
            u = ctgeom.VoxelSizeUncertaintyInputs(sigma_sod=sigma_sod, sigma_sdd=0.0, sigma_pixel=0.0)
            sigma_voxel = ctgeom.voxel_size_sigma_x(g, u)
            pct = (sigma_voxel / voxel) * 100.0
            row[f"sigma_sod_{sigma_sod}mm_voxel_sigma_um"] = round(sigma_voxel * 1000.0, 6)
            row[f"sigma_sod_{sigma_sod}mm_pct"] = round(pct, 4)

        # Also use magnification_off_axis directly: if the object sits
        # 0.05mm closer to the source than assumed (a single concrete
        # displacement, not a statistical sigma), how much does the
        # magnification at that point differ from the axis value?
        M_offset = ctgeom.magnification_off_axis(g, delta_from_axis=0.05)
        row["M_if_object_0.05mm_closer_to_source"] = round(M_offset, 4)
        row["M_pct_change_for_0.05mm_shift"] = round((M_offset - M_actual) / M_actual * 100.0, 4)

        rows.append(row)

    out_path = os.path.join(os.path.dirname(__file__), "sensitivity_sweep.csv")
    fieldnames = list(rows[0].keys())
    with open(out_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"Wrote {out_path}\n")
    print(f"{'M':>8} {'voxel (um)':>12} {'+/-0.005mm SOD err -> %':>26} {'+/-0.05mm SOD err -> %':>25} {'0.05mm shift -> M %chg':>24}")
    for row in rows:
        print(f"{row['magnification']:>8.2f} {row['voxel_size_um']:>12.4f} "
              f"{row['sigma_sod_0.005mm_pct']:>25.3f}% {row['sigma_sod_0.05mm_pct']:>24.3f}% "
              f"{row['M_pct_change_for_0.05mm_shift']:>23.3f}%")

    print("\n--- Plain-language summary ---")
    low = rows[0]
    high = rows[-1]
    print(f"At the lowest magnification tested ({low['magnification']}x, SOD={low['sod_mm']:.1f}mm), "
          f"a 0.05mm object-positioning error changes voxel size by {low['sigma_sod_0.05mm_pct']:.3f}%.")
    print(f"At the highest magnification tested ({high['magnification']}x, SOD={high['sod_mm']:.2f}mm), "
          f"the SAME 0.05mm error changes voxel size by {high['sigma_sod_0.05mm_pct']:.3f}% -- "
          f"roughly {high['sigma_sod_0.05mm_pct'] / low['sigma_sod_0.05mm_pct']:.0f}x more sensitive.")
    print("This confirms the expected relationship: at high magnification the object sits very")
    print("close to the source (small SOD), so a fixed positioning error is a much larger fraction")
    print("of SOD itself, and voxel-size uncertainty grows accordingly. This is why high-magnification")
    print("micro-CT scans place tighter demands on mechanical stage precision than low-magnification ones.")


if __name__ == "__main__":
    main()
