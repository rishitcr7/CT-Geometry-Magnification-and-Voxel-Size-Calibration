# ctgeom

CT cone-beam geometry, magnification, and voxel-size calibration module.
Core logic in C++, with Python bindings via pybind11.

## Layout

```
include/ctgeom/
  geometry.hpp          Magnification, voxel size, off-axis magnification, error propagation
  xtekct_parser.hpp     Parser for Nikon .xtekct files
  ctinfo_parser.hpp     Parser for Nikon .ctinfo.xml reports (independent 2nd source)
  ctprofile_parser.hpp  Parser for Nikon .ctprofile.xml (reconstruction settings, VOI crop)
  calibration.hpp       Reference-sphere voxel-size calibration (circle fit, voxel size vs
                         stage position line fit, synthetic phantom generator)
  image_edge.hpp        Sub-pixel sphere-edge finder for projection images (synthetic-tested)
  export.hpp            Geometry -> ASTRA `cone` arguments and TIGRE attributes (arithmetic only)
  compare.hpp           Calculated-vs-reported comparison; cross-check between .xtekct
                         and .ctinfo.xml; VOI-crop-vs-voxel-dimensions consistency check
src/cli.cpp              CLI: parse a .xtekct (+ auto-detected .ctinfo.xml and
                         .ctprofile.xml), print all comparisons and cross-checks
src/sensitivity_report.cpp  Sensitivity/tolerance analysis: how much real-world
                         positioning error it takes to trip the compare
                         tolerances, off-axis magnification, uncertainty propagation
python/bindings.cpp       pybind11 module exposing all of the above to Python
tests/test_geometry.cpp   Unit tests: geometry, parsers, cross-checks (64 assertions)
tests/test_calibration.cpp  Unit tests: calibration + scaled tolerance, synthetic data (41)
tests/test_image_edge.cpp  Unit tests: edge finder, end-to-end calibration from images, export (23)
CMakeLists.txt           CMake build (library, CLI, tests, optional Python module)
data/                     Sample real scan files for testing
analysis/                 Synthetic sensitivity sweep (Python) across the
                         scanner's full magnification range — see analysis/README.md
```
## Data
Data: the real Nikon scan files are not included in this repository. test_geometry and the CLI demo read them from data/. To run them, put your own .xtekct, _ctinfo.xml and _ctprofile.xml files in a data/ folder. The other two test programs use only simulated data and run without it.

## Windows quick start

1. Install MSYS2 (msys2.org). In the "MSYS2 UCRT64" terminal run:
   `pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake`
2. Put `C:\msys64\ucrt64\bin` at the top of your PATH and restart the terminal.
   `g++ --version` must report GCC 7 or newer (C++17 needs it; GCC 6 fails on `<optional>`).
3. Build, test and run from this folder (PowerShell):

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
.\build\ctgeom_cli.exe data\poly_isro_mioni1_fbp_01.xtekct
```

Real-scan output is saved in `results/`.

## Build with CMake (alternative to the g++ lines below)

```
cmake -S . -B build            # add -DCTGEOM_BUILD_PYTHON=ON for the Python module
cmake --build build -j
cd build && ctest --output-on-failure
```

## Build (C++ CLI + tests)

```
g++ -std=c++17 -O2 -Iinclude src/cli.cpp -o ctgeom_cli
g++ -std=c++17 -O2 -Iinclude tests/test_geometry.cpp -o test_geometry
./test_geometry
./ctgeom_cli data/poly_isro_mioni1_fbp_01.xtekct
```

The CLI auto-detects matching `<Name>_ctinfo.xml` and `<Name>_ctprofile.xml`
files in the same folder as the `.xtekct` file and runs both cross-checks
automatically; pass paths explicitly to override:
```
./ctgeom_cli data/poly_isro_mioni1_fbp_01.xtekct data/poly_isro_mioni1_fbp_ctinfo.xml data/poly_isro_mioni1_fbp_ctprofile.xml
```
Exit code is non-zero if anything is flagged outside tolerance: the
internal voxel-size check, the `.ctinfo.xml` cross-check, or the
`.ctprofile.xml` VolumeOfInterest-vs-voxel-dimensions check.

### Sensitivity / tolerance analysis (no new scan data needed)

```
g++ -std=c++17 -O2 -Iinclude src/sensitivity_report.cpp -o sensitivity_report
./sensitivity_report data/poly_isro_mioni1_fbp_01.xtekct
```
Answers: how much real SOD positioning error (in mm) does it actually
take to trip the 0.5% compare tolerance, at the real scan's geometry and
at two synthetic reference geometries (near-unity and 100x magnification)?
Also runs `magnification_off_axis()` and `voxel_size_sigma_x()` — both
written earlier but previously never executed against anything — against
the real scan's geometry.

See `analysis/README.md` for the companion Python sweep
(`analysis/sensitivity_sweep.py`) across the scanner's full 1.5x-300x
magnification range, and its finding on the tolerance.

## Build (Python module)

```
pip install pybind11
PYBIND_INC=$(python3 -c "import pybind11; print(pybind11.get_include())")
PY_INC=$(python3 -c "import sysconfig; print(sysconfig.get_path('include'))")
EXT=$(python3 -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX'))")
g++ -std=c++17 -O2 -shared -fPIC -Iinclude -I"$PYBIND_INC" -I"$PY_INC" \
    python/bindings.cpp -o ctgeom"$EXT"
```

```python
import ctgeom
r = ctgeom.parse_xtekct("data/poly_isro_mioni1_fbp_01.xtekct")
g = r.to_geometry()
print(ctgeom.magnification(g), ctgeom.voxel_size_x(g))
```

## Status

Validated against a real Nikon XT H 225 ST scan (`poly_isro_mioni1_fbp`):

- Calculated magnification and voxel size match the values Nikon's
  *reconstruction* software wrote to `.xtekct`, to within ~3e-5%.
- The same calculated values independently cross-check against the
  values Nikon's *acquisition* software wrote to the separate
  `.ctinfo.xml` report (a different file, written by a different part
  of the pipeline, read with its own independent parser) — also within
  ~3e-5%, confirming both files describe the same, self-consistent scan.
- Verified the cross-check actually catches a real mismatch: artificially
  corrupting SOD in a copy of the `.xtekct` file was correctly flagged
  in three independent places (internal voxel-size check, SOD cross-check,
  and magnification cross-check), with the CLI's exit code flipping to
  failure — confirming this is usable as an automated sanity check, not
  just a passive report.
- A THIRD independent real file, `.ctprofile.xml` (reconstruction
  settings), is now also parsed and cross-checked: its `VolumeOfInterest`
  crop region (the pixel-space box the reconstruction software cut out
  of the full detector image) exactly predicts the `VoxelsX/Y/Z`
  dimensions Nikon wrote into `.xtekct` for this job (1026 x 975 x 2850
  in both). Note: the one sample reconstructed slice provided
  (`recon0094.tif`) is 1000x1000, which does not match this scan's
  1026x975 slice size. That mismatch is a fact; its cause is NOT
  verified (different reconstruction crop, different scan, or
  something else), so the slice cannot be tied to this file set yet.

128 unit tests pass in total (18 on the core math/parser, 18 on the
`.ctinfo.xml` cross-check, 18 on `.ctprofile.xml` and the VOI
consistency check, 10 more on the off-axis uniformity check below),
including round-trips against all three real uploaded files and several
deliberately-broken inputs to confirm mismatches are actually caught,
not just that matches pass.

**Without a second real scan, answered the tolerance question a different
way**: built a sensitivity analysis (`src/sensitivity_report.cpp` +
`analysis/sensitivity_sweep.py`, cross-checked against each other in C++
and Python independently) that finds the fixed 0.5% compare tolerance is
well-calibrated at this project's real scan's magnification (~7.3x) —
comfortably above the ~0.03% of noise ordinary stage-positioning error
would cause — but becomes too loose at high magnification (100-300x,
within this scanner's supported range), where realistic positioning
error alone can exceed 0.5% and would be missed rather than flagged.
Documented as an open recommendation in `analysis/README.md` rather than
silently changed, since tuning it without a real high-magnification scan
to validate against would be a guess, not a fix.

**`magnification_off_axis()` is now a real, automated check**, not just
informational code run once in a report: `compare.hpp`'s
`check_off_axis_uniformity()` computes the field-of-view half-width
directly from each scan's own reported volume dimensions, compares
edge-vs-axis magnification, and flags when the difference exceeds a
configurable threshold (default 2%). Run on the real scan: **7.36%**,
which is flagged — a real, non-trivial effect, not a rounding artifact.
Deliberately does NOT affect the CLI's exit code or `any_flagged`: unlike
the other checks, a large value here reflects expected cone-beam physics
(voxel size is a volume-wide approximation; it's least accurate at the
field-of-view edges), not a data-consistency problem, so it's reported
as a note rather than a failure. 10 new tests cover this (zero-size
degenerate case, the real scan's actual effect, and that the
significance threshold is independently configurable from the
underlying computed value) — **64 tests in test_geometry, plus 41 in test_calibration and 23 in test_image_edge (128 total); all passing.**

## Reference-sphere calibration (synthetic-data tested only)

`calibration.hpp` implements the Yang et al. (2014) method: fit a circle to a
reference sphere's silhouette at several stage positions, convert to voxel
size (V = true diameter / pixel diameter), then fit V against stage position.
The fitted line gives implied SDD (= pixel / slope) and implied SOD
(= intercept / slope), which `compare_calibration_to_geometry()` checks
against the nominal geometry.

```
g++ -std=c++17 -O2 -Iinclude tests/test_calibration.cpp -o test_calibration && ./test_calibration
```

What is and isn't established: on synthetic sphere data built from the real
scan's geometry, the code recovers SDD, SOD and voxel size (exactly without
noise; within 0.5% with 0.1 px edge noise) and flags a 5% wrong nominal SOD.
It starts from edge POINTS; finding the edge in a real projection is not
implemented, and nothing here has been run on a real reference-object scan.

**Edge finder (`image_edge.hpp`):** casts rays from an approximate centre,
finds the steepest change and refines to the half-contrast crossing. On
synthetic blurred-disc images it recovers the diameter to ~0.002 px without
noise and ~0.004 px at 3% pixel noise (tests allow 0.05 and 0.3 px), and the
full chain image -> edge -> circle -> calibration recovers SDD, SOD and voxel
size within 0.5%. Real projections have beam hardening, scatter and a
non-flat background that are not modelled, so accuracy on real data is unknown.

**GPU toolbox export (`export.hpp`):** `to_astra_cone()` and `to_tigre()`
turn a Geometry + voxel size into ASTRA/TIGRE arguments (ASTRA wants voxel
units, TIGRE mm). Checked against the documented conventions only; neither
toolbox has been installed or run. See the GPU/ASTRA/TIGRE feasibility doc.

**Magnification-aware tolerance (opt-in):** `scaled_tolerance_pct()` /
`cross_check_scaled()` raise the tolerance at high magnification, where a
fixed positioning error is a larger fraction of SOD. Defaults elsewhere are
unchanged. The 0.05 mm positioning sigma is an illustrative placeholder, not
a measured scanner figure. Note `compare_record()` compares values from the
same file (same SOD), where positioning error cancels, so the fixed 0.5%
default was already adequate there; scaling matters mainly for comparing
SOD values recorded at different times.

## Not yet done

- Only ONE real scan has been tested end-to-end (all three of its report
  files agree with each other and with the calculated values). The tool
  should be run against several more `.xtekct`/`.ctinfo.xml`/`.ctprofile.xml`
  triples once available, to see whether the current 0.5%/0.01% tolerance
  thresholds hold up at THIS scan's magnification and, per the sensitivity
  analysis above, whether they should scale with magnification for other
  scans. No new scan data has been provided yet, so real-data validation
  of this remains blocked; the sensitivity analysis is the synthetic
  substitute available in the meantime.
- Real reference-object calibration (Section 8 of the Plan of Action): the
  fitting code and edge finder exist and pass synthetic tests, but there is
  no real scan of a known-size object to run them on.
- The ~100 reconstructed slice images (`reconNNNN.tif`) still haven't been
  used for an actual physical measurement/sanity check — only one sample
  slice has been inspected. Its size (1000x1000) does not match the
  `.xtekct` volume (1026x975), its origin is unverified, and it showed
  no measurable edge, so it can't be used for that check as-is.
- Running ASTRA/TIGRE: needs a GPU machine and real projection data. Only the
  feasibility write-up and the geometry export are done.

