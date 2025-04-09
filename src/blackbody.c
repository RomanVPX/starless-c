#include "blackbody.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h> // For NULL

// Define the global pointer for the ramp texture
Texture *color_temp_ramp = NULL;

// Constant from Python code: 3/4 * log(3)
const double LOGSHIFT = 0.823959216501;
// Constant for intensity calculation: 29622.4 K (related to hc / (lambda_peak * k_B))?
const double INTENSITY_TEMP_CONST = 29622.4;

bool load_color_ramp(const char *filename) {
    if (color_temp_ramp != NULL) {
        fprintf(stderr, "Warning: Color ramp already loaded. Freeing existing one.\n");
        free_color_ramp();
    }
    // Use the image loading function. Assume sRGB state doesn't matter here as we use raw values.
    color_temp_ramp = load_texture(filename);
    if (!color_temp_ramp) {
        fprintf(stderr, "Error: Failed to load color temperature ramp '%s'\n", filename);
        return false;
    }
    // Check if it's a 1D ramp (height should be 1 or small)
    if (color_temp_ramp->height != 1) {
         fprintf(stderr, "Warning: Color temperature ramp '%s' has height %d (expected 1).\n",
                 filename, color_temp_ramp->height);
                 // Proceeding anyway, assuming we only read the first row.
    }

    printf("Loaded color temperature ramp: %s (%d pixels wide)\n", filename, color_temp_ramp->width);
    return true;
}

void free_color_ramp(void) {
    if (color_temp_ramp) {
        free_texture(color_temp_ramp);
        color_temp_ramp = NULL;
        printf("Freed color temperature ramp.\n");
    }
}

// Calculate log temperature T(R) = T_isco * (R / R_isco)^(-3/4)
// log(T(R)) = log(T_isco) - 3/4 * log(R/R_isco)
// Assuming R_isco = 3 (where logshift comes from?), then
// log(T(R)) = log(T_isco) - 3/4 * (log(R) - log(3))
// log(T(R)) = log(T_isco) + 3/4*log(3) - 3/4 * log(R)
// log(T(R)) = log(T_isco) + LOGSHIFT - 3/8 * log(R^2)
double bb_log_temperature(double sqr_radius, double log_T0_isco) {
    // Ensure sqr_radius is positive to avoid log domain error
    if (sqr_radius <= 0) return -INFINITY; // Or some very low temp indicator

    double A = log_T0_isco + LOGSHIFT;
    return A - 0.375 * log(sqr_radius);
    // Note: Python code uses base 10 log implicitly via `np.log`? Check numpy docs.
    // `np.log` is natural log (base e). Okay.
}

// Intensity ~ 1 / (exp(const / T) - 1)
// Approximates integral of Planck's law over visible spectrum peak?
double bb_intensity(double temperature) {
    // Clamp temperature to avoid division by zero or negative temps
    temperature = fmax(1.0, temperature); // Prevent T=0 or T<0 issues
    double exp_term = exp(INTENSITY_TEMP_CONST / temperature);
    // Avoid potential overflow if exp_term is huge or division by zero if exp_term is 1
    if (isinf(exp_term) || exp_term <= 1.0) {
        return 0.0; // Effectively zero intensity for very low T or errors
    }
    return 1.0 / (exp_term - 1.0);
}

// Lookup color from the ramp based on temperature
ColorRGB bb_color_from_temp(double temperature) {
    if (!color_temp_ramp || !color_temp_ramp->data) {
        fprintf(stderr, "Error: Color ramp not loaded for bb_color_from_temp lookup.\n");
        return COLOR_BLACK;
    }

    int ramp_size = color_temp_ramp->width;
    // Map temperature range (approx 1000K to 30000K from Python?) to ramp index [0, ramp_size-1]
    // Python: (T-1000)/29000. * rampsz
    double normalized_temp = (temperature - 1000.0) / 29000.0;

    // Clamp normalized temp to [0, 1] range before scaling
    normalized_temp = fmax(0.0, fmin(1.0, normalized_temp));

    // Calculate index, ensuring it stays within bounds
    int index = (int)(normalized_temp * (ramp_size - 1.0001)); // Match Python's index calc
    index = fmax(0, fmin(ramp_size - 1, index)); // Clamp index just in case

    // Calculate memory offset in the texture data buffer
    // Assumes height is 1 (or we only care about the first row)
    // Assumes 3 channels (RGB)
    int offset = index * 3; // 3 bytes per pixel (RGB)

    // Read RGB values and normalize to [0, 1]
    ColorRGB color;
    color.r = color_temp_ramp->data[offset + 0] / 255.0;
    color.g = color_temp_ramp->data[offset + 1] / 255.0;
    color.b = color_temp_ramp->data[offset + 2] / 255.0;

    // Note: Python loads ramp as uint8/255. Does it convert sRGB->Linear?
    // The Python code doesn't show srgb->linear conversion for the ramp specifically.
    // Let's assume the ramp JPG is already effectively linear or the distortion is acceptable.
    // If colors look wrong, we might need to apply srgb_to_linear here.

    return color;
}
