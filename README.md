# Starless-C: A Black Hole Raytracer in C

[![Build Status](https://github.com/RomanVPX/starless-c/actions/workflows/build.yml/badge.svg)](https://github.com/RomanVPX/starless-c/actions)

This project is a C port and an extension of the original Python-based black hole raytracer "Starless" by Riccardo Antonelli ([rantonels/starless](https://github.com/rantonels/starless)).
The goal is to provide a faster, more extensible, and cross-platform version while maintaining compatibility with the original scene file format and exploring new rendering features.

The original "Starless" is a CPU black hole raytracer in NumPy, designed for informative diagrams and wallpaper material, performing full geodesic raytracing in Schwarzschild geometry. You can read more about the original project on its [project page](http://rantonels.github.io/starless/) and [wiki](https://github.com/rantonels/starless/wiki).

## Core Features (Many Ported from Original)

*   Full geodesic raytracing in Schwarzschild geometry.
*   Accretion disk rendering with alpha-blending.
*   Optional blackbody mode for the accretion disk, including realistic redshift effects (Doppler + gravitational).
*   Distortion of the background sky.
*   Dust rendering (ported from the original).
*   Post-processing effects:
    *   Airy Disk bloom (ported from the original, for physically-based diffraction bloom).
    *   Bloom (Gaussian blur-based, ported from the original).
*   Multi-threaded rendering for performance.
*   Compatibility with the original `.scene` file format.

**Core Changes & Improvements:**

*   **Performance:** While the original Python version also utilized multiprocessing, this C implementation is inherently significantly faster due to the language choice, offering substantial reductions in rendering times.
*   **Cross-Platform Builds:** GitHub Actions automatically build and test the tracer on Windows, macOS, and Linux.
*   **Blackbody LUT Generation:** Includes a Python script to generate high-precision textual Look-Up Tables (LUTs) for blackbody radiation colors over an arbitrary temperature range. The script also outputs a visual representation of the ramp.
*   **Blackbody Rendering:** The blackbody temperature calculation logic has been updated to use these new LUTs, aiming for results comportementally close to the original when using the same temperature range.
*   **Blending Modes:** The blending for the accretion disk has been revised for more standard alpha blending (Porter-Duff "over" operator). The original blending behavior can be enabled via a compile-time define for comparison.
*   **Disk Alpha Calculation:** The method for calculating disk alpha in Blackbody mode has been refined for better control over edge falloff. The original behavior can also be enabled via a define.
*   **PNG Metadata:** The rendered PNG images now embed configuration parameters used for the render. Currently, this is implemented by saving a JSON representation of some settings into a comment field, with plans to improve this for better readability with EXIF viewers and more comprehensive coverage of all settings.

**New Rendering Features:**

*   **ACES Tonemapping:** Added ACES (Academy Color Encoding System) filmic tonemapping for improved HDR to LDR conversion, providing more cinematic and perceptually accurate results.
*   **SSAA (Supersampling Anti-Aliasing):** Implemented Supersampling Anti-Aliasing with jittered samples to reduce aliasing artifacts and improve image quality, especially on fine details like the photon sphere.
*   **Procedural Disk Structures:** Added an option to procedurally generate structures (rings, spirals, variations) within the accretion disk in Blackbody mode. This enhances visual detail and helps in understanding the disk's geometry without relying on a texture.
*   **Flexible Textures:** Sky and disk textures are no longer hardcoded and can be specified in scene files.

**Compatibility & Included Assets:**

*   Maintains backward compatibility with the original `.scene` file format.
*   All original scenes from `rantonels/starless` are included in the `scenes/original/` directory for testing and comparison.
*   Original textures (`adisk.jpg`, `bgedit.png`) from `rantonels/starless` are also included in `textures/` for reproducibility. Their original licenses are as per the `rantonels/starless` repository. `bgedit.png` appears to be a modified version of a NASA starmap.
*   Some new scenes (in `scenes/new/`) utilize an 8K Milky Way panorama texture (`textures/starmap_2020_8k.png`) sourced from NASA's Scientific Visualization Studio (converted from original EXR to PNG).

**Features from Original Not (Yet) Implemented in Starless-C:**

*   **Real-time Preview / Dynamic Chunking:** The original Python version had features for a more progressive/shuffled render preview and potentially dynamic chunk re-assignment to free processes. This C port currently uses a simpler static chunk distribution.
*   **Matplotlib Scene Visualization:** The original could generate a Matplotlib diagram of the scene setup. This is not part of the C port.
*   **Intermediate Image Outputs:** The original project saved several intermediate images during different rendering stages (e.g., pre-postprocessing). This C port currently outputs only the final processed image.

## Building

For now, check the GitHub Actions workflow for build steps.

## Usage

```bash
./starless_tracer [options] [scene_file.scene]
```

**Examples:**
To render a scene:
```bash
./starless_tracer scenes/new/default_blackbody.scene
```
This command should produce a .png image similar to this in the `out/` directory:
![Render of the default_blackbody.scene](docs/default_blackbody_(H)_001.jpg)

To render in "lo-fi" mode (uses the [lofi] section from the scene file):
```bash
./starless_tracer -d scenes/original/default_blackbody.scene
```

## Key Differences from the Original Python Version Summarized:

*   **Language & Performance:** C vs. Python/NumPy, resulting in significant speed-ups.
*   **Blackbody Color Source:** Textual LUT generated via Python script vs. hardcoded image ramp.
*   **Tonemapping:** ACES added.
*   **Anti-Aliasing:** SSAA added.
*   **Disk Detail:** Procedural disk structures added.
*   **Dependencies:** Fewer runtime dependencies (no Python, NumPy, PIL, Matplotlib needed for the core tracer).
*   **Render Preview:** The current C version has a more basic progressive rendering output per thread compared to the potentially more dynamic preview of the original.
*   **Metadata Storage:** This C version saves configuration into PNG metadata (work in progress for comprehensiveness and format).

## TODO / Future Work

*   Improve PNG metadata storage (more fields, better format for EXIF viewers).
*   Optionally implement saving of intermediate rendering stages.
*   Explore more advanced/efficient anti-aliasing techniques (e.g., adaptive SSAA).
*   Investigate more sophisticated procedural noise/turbulence for disk structures.
*   Further performance optimizations (e.g., SIMD, Airy bloom optimization).

## Acknowledgements

*   **Riccardo Antonelli ([rantonels](https://github.com/rantonels))** for creating the original "Starless" and for the insightful [article](http://rantonels.github.io/starless/) explaining the physics and implementation details. This C port would not exist without his foundational work.
*   **stb_image, stb_image_write_ext, stb_image_resize** by Sean Barrett and contributors for image loading, saving, and resizing.
*   **inih (INI Parser)** by Ben Hoyt for INI file parsing.
*   **NASA/Goddard Space Flight Center Scientific Visualization Studio** for the Milky Way panorama texture used in some example scenes.

## License

GPL-3.0 license (to maintain compatibility with the original project).

