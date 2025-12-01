# Changelog

All notable changes to the Starless-C project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
