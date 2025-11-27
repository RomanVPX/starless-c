import argparse
import sys
import colour
from pathlib import Path

import numpy as np
import imageio.v3 as iio


# Defaults
DEFAULT_TEMP_MIN = 1000.0
DEFAULT_TEMP_MAX = 50000.0
DEFAULT_NUM_SAMPLES = 4096
PNG_HEIGHT = 64


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate blackbody color ramp in linear sRGB."
    )
    parser.add_argument(
        "--temp-min", type=float, default=DEFAULT_TEMP_MIN,
        help=f"Minimum temperature in Kelvin (default: {DEFAULT_TEMP_MIN})"
    )
    parser.add_argument(
        "--temp-max", type=float, default=DEFAULT_TEMP_MAX,
        help=f"Maximum temperature in Kelvin (default: {DEFAULT_TEMP_MAX})"
    )
    parser.add_argument(
        "-n", "--num-samples", type=int, default=DEFAULT_NUM_SAMPLES,
        help=f"Number of samples in the ramp (default: {DEFAULT_NUM_SAMPLES})"
    )
    parser.add_argument(
        "-o", "--output-dir", type=Path, default=Path("."),
        help="Output directory for generated files (default: current directory)"
    )
    return parser.parse_args()


def compute_blackbody_ramp(temp_min: float, temp_max: float, num_samples: int) -> tuple[np.ndarray, float]:
    """
    Compute linear sRGB colors for blackbody temperatures.
    Returns the ramp array (num_samples, 3) and the maximum channel value.
    """
    observer = 'CIE 1964 10 Degree Standard Observer'
    cmfs = colour.MSDS_CMFS[observer]
    illuminants = colour.CCS_ILLUMINANTS[observer]
    print(f"Using observer: {cmfs.name}")

    illuminant_to_xyz = colour.SDS_ILLUMINANTS["E"]
    print(f"Using illuminant: {illuminant_to_xyz.name}")
    source_xyz_illuminant = illuminants['E']
    print(f"Source illuminant: {source_xyz_illuminant}")

    temperatures = np.linspace(temp_min, temp_max, num_samples)
    ramp = np.zeros((num_samples, 3), dtype=np.float64)
    progress_step = max(1, num_samples // 10)

    print(f"Calculating {num_samples} blackbody colors from {temp_min:.0f}K to {temp_max:.0f}K...")

    output_colourspace = colour.RGB_COLOURSPACES['sRGB']

    for i, temp in enumerate(temperatures):
        try:
            spd = colour.sd_blackbody(float(temp), cmfs.shape)
            xyz = colour.sd_to_XYZ(spd, cmfs, illuminant_to_xyz) / 100.0
            rgb = colour.XYZ_to_RGB(xyz, output_colourspace, source_xyz_illuminant)
            ramp[i] = np.clip(rgb, 0.0, None)
        except Exception as e:
            print(f"\nError at {temp:.2f}K: {e}. Using black.")
            ramp[i] = 0.0

        if (i + 1) % progress_step == 0:
            print(f"  Progress: {i + 1}/{num_samples}")

    max_val = float(ramp.max())
    print(f"\nCalculation complete. Max value: {max_val:.4f}")
    return ramp, max_val


def save_ramp(filepath: Path, ramp: np.ndarray) -> None:
    """Save ramp data to a text file."""
    try:
        with filepath.open('w') as f:
            for rgb in ramp:
                f.write(f"{rgb[0]:.9f} {rgb[1]:.9f} {rgb[2]:.9f}\n")
        print(f"  Saved: {filepath}")
    except IOError as e:
        print(f"Error writing {filepath}: {e}")


def save_png_preview(filepath: Path, ramp_normalized: np.ndarray, height: int) -> None:
    """Convert normalized linear sRGB to sRGB and save as PNG."""
    try:
        # Apply sRGB OETF (gamma encoding)
        ramp_srgb = colour.cctf_encoding(ramp_normalized, function='sRGB')
        ramp_uint8 = (np.clip(ramp_srgb, 0.0, 1.0) * 255.999).astype(np.uint8)

        # Tile vertically
        image = np.tile(ramp_uint8[np.newaxis, :, :], (height, 1, 1))
        iio.imwrite(filepath, image)
        print(f"  Saved: {filepath}")
    except Exception as e:
        print(f"Error saving PNG: {e}")


def main() -> None:
    args = parse_args()

    print(f"colour-science version: {colour.__version__}")
    if sys.version_info < (3, 9):
        print("Warning: Python 3.9+ recommended for colour-science.")

    # Compute ramp
    ramp_raw, max_val = compute_blackbody_ramp(
        args.temp_min, args.temp_max, args.num_samples
    )

    # Normalize
    if max_val > 1e-9:
        ramp_normalized = ramp_raw / max_val
    else:
        print("Warning: Max value near zero, skipping normalization.")
        ramp_normalized = ramp_raw

    # Build filenames
    base = f"bb_ramp_{int(args.temp_min)}_{int(args.temp_max)}K_{args.num_samples}_linear_srgb"
    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    raw_path = output_dir / f"{base}.ramp"
    norm_path = output_dir / f"{base}_normalized.ramp"
    png_path = output_dir / f"{base}_preview.png"

    # Save files
    print(f"\nSaving {args.num_samples} colors...")
    save_ramp(raw_path, ramp_raw)
    save_ramp(norm_path, ramp_normalized)

    print(f"\nGenerating PNG preview ({args.num_samples}x{PNG_HEIGHT})...")
    save_png_preview(png_path, ramp_normalized, PNG_HEIGHT)

    print("\nDone.")


if __name__ == "__main__":
    main()
