# Changelog

All notable changes to the Starless-C project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.4.0] - 2026-04-19

### Added

- **Radial Opacity Falloff**: Added a new optional setting for the blackbody accretion disk (`Diskopacityfalloff`, `Diskopacityfalloffexp`). When it’s on, the outer parts of the disk gradually become more transparent based on their relative surface brightness (∝ T^n), using the rest-frame temperature. It’s off by default. Example usage can be found in `scenes/new/default_transparent.scene`.
- **Bicubic Texture Filtering**: In HiFi mode textures (sky and disk) are now sampled with bicubic (Catmull-Rom) filtering with centered sampling. LoFi mode keeps the original nearest-neighbor lookup.
- **Adaptive SSAA**: Optional two-pass adaptive supersampling (`SSAAAdaptive`, `SSAAThreshold`, `SSAADebugMask`). The tracer first renders at 1 sample per pixel, builds a hot-pixel mask from 3×3 luma max−min and a 1-ring dilation, then re-renders only the marked pixels with the full `SSAA`×`SSAA` jittered grid. Typically refines 10–25% of pixels with matching quality for a 3–5× speed-up. Off by default; when enabled, the mask can optionally be saved as `out/<scene>_ssaa_mask.png`.

### Changed

- **Disk Structure**: The procedural disk structure pattern is now applied to the blackbody temperature rather than the final color. This results in more physically accurate spectral shifts (reddening/blueing) in structured areas, rather than simple intensity modulation.

### Removed

- **Sky Texture Upscaling**: The HiFi-mode `resize_texture` pass (2× upscale of the sky texture) and the bundled `stb_image_resize.h` dependency have been removed; smoothness is now provided by the bicubic sampler directly, without any extra memory footprint.

### Fixed

- **`scenes/new/rings.scene`**: Replaced camera setup inherited from the `rantonels/starless` original to avoid a black frame; moved it for a clean edge-on view of the lensed disk.

## [0.3.1] - 2026-01-02

### Fixed

- **Windows SSAA**: Fixed a critical RNG scaling issue causing black output when SSAA is enabled on Windows (due to `RAND_MAX` mismatch) ([#37](https://github.com/RomanVPX/starless-c/issues/37)).

## [0.3.0] - 2026-01-01

### Added

- **Progress Reporting**: Added a real-time progress percentage indicator to the CLI output during the ray tracing phase.
- **Example scenes**: Added two example scenes (`simple_nomore_bb_structured_horizontal.scene` and `taglio_blackbody_horizontal.scene`) to the `scenes/new` directory.

### Changed

- **Dynamic Load Balancing**: Replaced static thread partitioning with dynamic load balancing for both Ray Tracing and Bloom effects.
- **Parallelization Infrastructure**: Introduced a unified `parallel_run` module to abstract threading logic and task management, simplifying the codebase.
- **Airy Bloom Optimization**: Implemented FFT-based convolution for Airy disk bloom. This calculates the OTF directly in the frequency domain, dramatically (order of magnitude) improving performance for large bloom radii compared to the original spatial convolution method.
- **Gaussian Bloom**: Implemented parallelization for Gaussian bloom.
- **Performance**: Enabled `-ffast-math` compiler flag and Link Time Optimization (`-flto`) for *significantly* faster rendering.
- **Math Compatibility**: Refactored blackbody temperature logic to ensure compatibility with `-ffast-math` (replaced `-INFINITY` with `-DBL_MAX`, moved runtime `NaN` checks to the LUT generation script).

## [0.2.0] - 2025-12-01

### Added

- **Enum values in PNG metadata**: Configuration enum values are now written to PNG metadata for better reproducibility.
- **Software version in PNG metadata**: Rendered images now include the software version in their metadata.
- **Blackbody ramp generation improvements**: Command-line parameters for temperature range and resolution.
- **Detailed physics comments**: Added comments explaining the Shakura-Sunyaev disk model derivation in blackbody temperature calculations.
- **New scene files**: `default_values.scene` documenting all available parameters with their default values; additional test scenes (`default_sky_blackbody_test.scene`, `simple_nomore_bb_structured.scene`).

### Changed

- **Config parser refactoring**: Simplified parsing logic, improved macro naming, enhanced error handling and validation.
- **Timing precision**: Replaced `clock()` timing with `timespec` for more accurate render time measurements.
- **Blackbody ramp generation improvements**: CIE 1964 10-Degree Standard Observer for spectrum calculations (produces slightly warmer colors).
- **Assets**: Updated pre-generated blackbody ramp files (`.ramp`) and preview images to reflect generation improvements.
- **Documentation**: Updated README.md with Windows usage instructions, command-line options reference, and scene file format section.
- **Dependencies**: Updated `pillow` dependency to 11.3.0 in `blackbody_ramp/requirements.txt`.
- **Default scene updates**: Improved comments and organization in `default.scene`.

### Fixed

- **Tracer**: Fixed conditional logic for Redshift calculations.
- **Build**: Suppressed warnings for `stb_image_resize.h` on Windows builds.
- **Docs**: General documentation updates.
- **Minor log message improvements**: Clarified warning messages for unknown options.

## [0.1.5] - 2025-05-20

- **Initial public release** of Starless-C, a C port of the Python black hole raytracer [rantonels/starless](https://github.com/rantonels/starless).
- Full geodesic raytracing in Schwarzschild geometry.
- Accretion disk rendering with alpha-blending.
- Blackbody mode for the accretion disk with realistic redshift effects (Doppler + gravitational).
- Background sky distortion.
- Dust rendering.
- Post-processing effects: Airy Disk bloom and Gaussian blur-based bloom.
- Multi-threaded rendering.
- ACES (Academy Color Encoding System) filmic tonemapping.
- SSAA (Supersampling Anti-Aliasing) with jittered samples.
- Procedural disk structures for blackbody mode.
- Flexible sky and disk texture specification in scene files.
- PNG metadata embedding of render configuration.
- Compatibility with original `.scene` file format from rantonels/starless.
