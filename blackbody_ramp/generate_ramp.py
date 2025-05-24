import numpy as np
import colour
import sys
import imageio.v3 as iio

# --- Configuration ---
TEMP_MIN = 1000.0
TEMP_MAX = 50000.0
NUM_SAMPLES = 4096
# Use .ramp extension for the data file
RAMP_FILENAME = f"blackbody_ramp_{int(TEMP_MIN)}_{int(TEMP_MAX)}K_{NUM_SAMPLES}_linear_srgb_normalized.ramp"
# Filename for the PNG preview
PNG_FILENAME = f"blackbody_ramp_{int(TEMP_MIN)}_{int(TEMP_MAX)}K_{NUM_SAMPLES}_preview.png"
PNG_HEIGHT = 50 # Height of the preview image in pixels
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
if max_val <= 1e-9:
    print("Warning: Max value is near zero. Cannot normalize.")
    rgb_ramp_normalized = rgb_ramp_linear_raw
else:
    rgb_ramp_normalized = [rgb / max_val for rgb in rgb_ramp_linear_raw]

# --- Save the normalized float data to .ramp file ---
print(f"Saving {len(rgb_ramp_normalized)} normalized colors to {RAMP_FILENAME}...")
try:
    with open(RAMP_FILENAME, 'w') as f:
        for rgb in rgb_ramp_normalized:
            f.write(f"{rgb[0]:.9f} {rgb[1]:.9f} {rgb[2]:.9f}\n")
    print("  Data file saved.")
except IOError as e:
    print(f"\nError writing to file {RAMP_FILENAME}: {e}")


# --- Create and save the PNG preview ---
print(f"Generating PNG preview ({NUM_SAMPLES}x{PNG_HEIGHT}) to {PNG_FILENAME}...")
try:
    # Convert normalized linear float ramp to sRGB uint8 for PNG
    # 1. Convert linear RGB [0, ~1] to sRGB [0, ~1] (Apply OETF)
    rgb_ramp_srgb_float = [colour.RGB_to_RGB(rgb, 'sRGB', 'sRGB', apply_cctf_encoding=True) for rgb in rgb_ramp_normalized]

    # 2. Convert sRGB float [0, 1] to sRGB uint8 [0, 255]
    # Ensure values are clipped to [0, 1] before scaling, just in case
    rgb_ramp_srgb_uint8 = [(np.clip(rgb, 0.0, 1.0) * 255.999).astype(np.uint8) for rgb in rgb_ramp_srgb_float]

    # Create the image array (height, width, channels)
    # Convert list of RGB triplets to a NumPy array (width, channels)
    ramp_array = np.array(rgb_ramp_srgb_uint8) # Shape: (NUM_SAMPLES, 3)

    # Repeat the ramp vertically to create the desired height
    image_array = np.tile(ramp_array, (PNG_HEIGHT, 1, 1)) # Shape: (PNG_HEIGHT, NUM_SAMPLES, 3)

    # Save the image using imageio
    iio.imwrite(PNG_FILENAME, image_array)
    print("  PNG preview saved.")

except ImportError:
    print("\nError: imageio library not found. Cannot save PNG preview.")
    print("Please install it: pip install imageio")
except Exception as e:
    print(f"\nError generating or saving PNG preview: {e}")

print("\nScript finished.")
