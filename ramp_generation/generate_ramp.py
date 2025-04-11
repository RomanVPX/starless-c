import numpy as np
import colour
import sys

# --- Configuration ---
TEMP_MIN = 1000.0
TEMP_MAX = 30000.0
NUM_SAMPLES = 1024
# Use .ramp extension as requested
OUTPUT_FILENAME = f"blackbody_ramp_{int(TEMP_MIN)}_{int(TEMP_MAX)}K_{NUM_SAMPLES}_linear_srgb_normalized.ramp"
# --- End Configuration ---

print(f"Using colour-science version: {colour.__version__}")
if sys.version_info < (3, 7):
    print("Warning: Your Python version might be older. colour-science works best on Python 3.7+")

cmfs = colour.MSDS_CMFS['CIE 1931 2 Degree Standard Observer']
temperatures = np.linspace(TEMP_MIN, TEMP_MAX, NUM_SAMPLES)
rgb_ramp_linear_raw = [] # Store raw values first

print(f"Calculating {NUM_SAMPLES} blackbody colors from {TEMP_MIN:.0f}K to {TEMP_MAX:.0f}K...")
print("Intermediate color space: Linear sRGB (Rec.709 primaries, D65 whitepoint)")

max_val = 0.0

for i, temp in enumerate(temperatures):
    try:
        spd = colour.sd_blackbody(float(temp), cmfs.shape)
        xyz = colour.sd_to_XYZ(spd, cmfs=cmfs)
        rgb_linear = colour.XYZ_to_RGB(xyz / 100.0,
                                       colour.CCS_ILLUMINANTS['CIE 1931 2 Degree Standard Observer']['E'],
                                       colour.RGB_COLOURSPACES['sRGB'].whitepoint,
                                       colour.RGB_COLOURSPACES['sRGB'].matrix_XYZ_to_RGB)
        rgb_linear_clipped = np.clip(rgb_linear, 0.0, None)

        # Find the maximum component value across all samples
        current_max = np.max(rgb_linear_clipped)
        if current_max > max_val:
            max_val = float(current_max)

        rgb_ramp_linear_raw.append(rgb_linear_clipped)

        if (i + 1) % (NUM_SAMPLES // 10) == 0:
            print(f"  Progress: {i+1}/{NUM_SAMPLES}")

    except Exception as e:
        print(f"\nError calculating color for temperature {temp:.2f}K: {e}")
        print("Skipping this temperature and adding black.")
        rgb_ramp_linear_raw.append(np.array([0.0, 0.0, 0.0]))

print(f"\nCalculation complete. Found max value: {max_val:.4f}")

# --- Normalize the ramp ---
print("Normalizing ramp by dividing by max value...")
if max_val <= 1e-9: # Avoid division by zero if ramp is all black
    print("Warning: Max value is near zero. Cannot normalize.")
    rgb_ramp_normalized = rgb_ramp_linear_raw # Keep raw values (should be ~0)
else:
    rgb_ramp_normalized = [rgb / max_val for rgb in rgb_ramp_linear_raw]


print(f"Saving {len(rgb_ramp_normalized)} normalized colors to {OUTPUT_FILENAME}...")
try:
    with open(OUTPUT_FILENAME, 'w') as f:
        for rgb in rgb_ramp_normalized:
            f.write(f"{rgb[0]:.9f} {rgb[1]:.9f} {rgb[2]:.9f}\n")
    print("Done.")
except IOError as e:
    print(f"\nError writing to file {OUTPUT_FILENAME}: {e}")
