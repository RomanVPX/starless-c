import numpy as np
import colour
import sys

# --- Configuration ---
TEMP_MIN = 1000.0
TEMP_MAX = 30000.0
NUM_SAMPLES = 1024
OUTPUT_FILENAME = f"blackbody_ramp_{int(TEMP_MIN)}_{int(TEMP_MAX)}K_{NUM_SAMPLES}_linear_srgb.ramp"
# --- End Configuration ---

print(f"Using colour-science version: {colour.__version__}")
# Basic check for compatibility, although newer versions are generally fine
if sys.version_info < (3, 7):
    print("Warning: Your Python version might be older. colour-science works best on Python 3.7+")

# Standard observer CMFs (needed for spectral shape)
# Using the default CIE 1931 2-degree observer
cmfs = colour.MSDS_CMFS['CIE 1931 2 Degree Standard Observer']

# Generate temperatures
temperatures = np.linspace(TEMP_MIN, TEMP_MAX, NUM_SAMPLES)

# List to store linear sRGB results
rgb_ramp_linear = []

print(f"Calculating {NUM_SAMPLES} blackbody colors from {TEMP_MIN:.0f}K to {TEMP_MAX:.0f}K...")
print("Output color space: Linear sRGB (Rec.709 primaries, D65 whitepoint)")

for i, temp in enumerate(temperatures):
    # Calculate Spectral Power Distribution (SPD) for the blackbody temperature
    # Use the shape of the standard observer CMFs for consistency
    try:
        spd = colour.sd_blackbody(float(temp), cmfs.shape)

        # Convert SPD to CIE XYZ (relative to implicit illuminant E of the observer)
        # Using default CIE 1931 2-degree observer
        xyz = colour.sd_to_XYZ(spd, cmfs=cmfs) # Explicitly passing cmfs

        # Convert CIE XYZ to linear sRGB color space.
        # colour.XYZ_to_RGB handles chromatic adaptation (default: Bradford)
        # from the observer's implicit whitepoint (usually E) to the target space's whitepoint (D65 for sRGB).
        rgb_linear = colour.XYZ_to_RGB(xyz / 100.0, # XYZ_to_RGB often expects XYZ scaled to Y=1 for white
                                       colour.CCS_ILLUMINANTS['CIE 1931 2 Degree Standard Observer']['E'], # Source illuminant
                                       colour.RGB_COLOURSPACES['sRGB'].whitepoint, # Target illuminant (D65)
                                       colour.RGB_COLOURSPACES['sRGB'].matrix_XYZ_to_RGB) # Target matrix

        # Clip negative values that might arise from numerical inaccuracies or gamut issues
        rgb_linear_clipped = np.clip(rgb_linear, 0.0, None)

        rgb_ramp_linear.append(rgb_linear_clipped)

        # Optional: Print progress
        if (i + 1) % (NUM_SAMPLES // 10) == 0:
            print(f"  Progress: {i+1}/{NUM_SAMPLES}")

    except Exception as e:
        print(f"\nError calculating color for temperature {temp:.2f}K: {e}")
        print("Skipping this temperature and adding black.")
        rgb_ramp_linear.append(np.array([0.0, 0.0, 0.0]))


print(f"\nCalculation complete. Saving {len(rgb_ramp_linear)} colors to {OUTPUT_FILENAME}...")

# Save to text file (R G B per line, space-separated floats)
try:
    with open(OUTPUT_FILENAME, 'w') as f:
        for rgb in rgb_ramp_linear:
            # Format to a reasonable number of decimal places for float precision
            f.write(f"{rgb[0]:.9f} {rgb[1]:.9f} {rgb[2]:.9f}\n")
    print("Done.")
except IOError as e:
    print(f"\nError writing to file {OUTPUT_FILENAME}: {e}")
