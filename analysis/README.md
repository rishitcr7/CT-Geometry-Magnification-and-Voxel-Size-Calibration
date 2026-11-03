# Sensitivity sweep: how precision requirements scale with magnification

`sensitivity_sweep.py` is a synthetic analysis (no new scan data needed)
that puts the project's already-written but previously-unused
`magnification_off_axis` and `voxel_size_sigma_x` functions to work,
answering a concrete question: **how much does ordinary mechanical
positioning uncertainty affect computed voxel size, and does that
change across the scanner's magnification range?**

## Method

- Detector pixel pitch fixed at 0.15mm and SDD fixed at 1123.14mm (the
  real scan's values — same detector, only geometry varied).
- SOD swept to produce magnifications from 1.5x to 300x, matching the
  Nikon XT H 225 ST's published geometric magnification range.
- At each magnification, computed how much voxel size shifts for a
  few illustrative object-positioning uncertainties (0.005mm,
  0.02mm, 0.05mm) — representative of rotary-stage runout / mounting
  repeatability, not pulled from this specific system's spec sheet.
  **Getting a real stage-repeatability figure for the XT H 225 ST from
  Nikon's documentation (or the advisor) would let these numbers be
  stated with real confidence rather than as illustrative estimates.**

## Finding

Run: `PYTHONPATH=. python3 analysis/sensitivity_sweep.py` (needs the
compiled `ctgeom` Python module — see main README).

| Magnification | Voxel size | Voxel-size shift from a 0.05mm positioning error |
|---|---|---|
| 1.5x  | 100.0 µm | 0.007% |
| 7.35x (this project's real scan) | 20.4 µm | 0.033% |
| 50x   | 3.0 µm   | 0.223% |
| 150x  | 1.0 µm   | 0.668% |
| 300x  | 0.5 µm   | 1.335% |

The same physical positioning error matters roughly **200x more** at
300x magnification than at 1.5x. This is expected — at high
magnification the object sits very close to the source (small SOD), so
a fixed-size error is a much larger fraction of SOD itself — but
quantifying it matters for this project specifically.

## Why this matters for `ctgeom`

`compare.hpp`'s `compare_record()` uses a fixed 0.5% tolerance when
flagging a calculated-vs-reported voxel-size mismatch. That default was
chosen and validated against the one real scan this project has, which
runs at 7.35x — where 0.5% is comfortably above the ~0.033% of
sensitivity expected from ordinary positioning error, so it won't
false-flag normal noise.

But at higher magnifications the scanner supports (up to 300x), a
*normal, not-actually-wrong* 0.05mm positioning variation alone would
produce a 1.335% difference — which **exceeds** the current fixed 0.5%
tolerance and would be incorrectly flagged as a geometry inconsistency.

**Recommendation (now available as an opt-in function, `scaled_tolerance_pct()`; defaults unchanged, see main README for caveats):** the tolerance threshold
should probably scale with magnification rather than stay fixed —
e.g. tolerance_pct ≈ base_tolerance × (M / M_reference), or a
tolerance derived directly from `voxel_size_sigma_x()` given a known
stage-precision figure, rather than a flat percentage. This is a
one-line change in `compare.hpp` once a decision is made, but changing
it now — without a second real high-magnification scan to validate
against — risks tuning it on a hunch instead of data. Flagging as an
open design question rather than silently changing the default that's
currently validated against real data.
