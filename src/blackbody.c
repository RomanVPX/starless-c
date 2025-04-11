#include "blackbody.h"
#include "color.h" // Already included via blackbody.h
#include <math.h>
#include <stdio.h>
#include <stdlib.h> // For malloc, free, NULL
#include <string.h> // For fgets

// --- Module Constants ---
// Constant from Python code: 3/4 * log(3)
static const double LOGSHIFT = 0.823959216501;
// Constant for OLD intensity calculation: 29622.4 K
// static const double INTENSITY_TEMP_CONST = 29622.4; // No longer needed if using normalized ramp directly

// Temperature range mapped by the ramp (MUST match the generated .ramp file)
static const double RAMP_TEMP_MIN = 1000.0;
static const double RAMP_TEMP_MAX = 30000.0;

// --- Static Global Variables for Ramp Data ---
static ColorRGB *g_blackbody_ramp_data = NULL; // Pointer to allocated ramp data
static int g_blackbody_ramp_size = 0;       // Number of samples loaded

// --- Function Implementations ---

static int count_ramp_samples(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        return -1;
    }

    int count = 0;
    char line[256];
    double r, g, b;

    while (fgets(line, sizeof(line), file)) {
        // Skip empty lines or comment lines
        if (line[0] == '\n' || line[0] == '#' || line[0] == '\0') {
            continue;
        }

        // Only count lines that can be parsed as 3 floats
        if (sscanf(line, "%lf %lf %lf", &r, &g, &b) == 3) {
            count++;
        }
    }

    fclose(file);
    return count;
}

bool load_blackbody_ramp_from_file(const char *filename, int expected_samples) {
    if (g_blackbody_ramp_data != NULL) {
        fprintf(stderr, "Warning: Blackbody ramp already loaded. Freeing existing one first.\n");
        free_blackbody_ramp();
    }
    if (expected_samples <= 0) {
        expected_samples = count_ramp_samples(filename);
        if (expected_samples <= 0) {
            fprintf(stderr, "Error: Failed to determine sample count in '%s' or file is empty/invalid.\n", filename);
            return false;
        }
        printf("Auto-detected %d samples in blackbody ramp file.\n", expected_samples);
    }

    printf("Loading blackbody ramp from '%s' (%d samples)... ", filename, expected_samples);
    fflush(stdout);

    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening ramp file");
        return false;
    }

    // Allocate memory
    g_blackbody_ramp_data = (ColorRGB *)malloc(expected_samples * sizeof(ColorRGB));
    if (!g_blackbody_ramp_data) {
        fprintf(stderr, "Error: Failed to allocate memory for blackbody ramp (%d samples).\n", expected_samples);
        fclose(file);
        return false;
    }

    char line_buffer[256];
    int samples_read = 0;
    while (samples_read < expected_samples && fgets(line_buffer, sizeof(line_buffer), file)) {
        if (sscanf(line_buffer, "%lf %lf %lf",
                   &g_blackbody_ramp_data[samples_read].r,
                   &g_blackbody_ramp_data[samples_read].g,
                   &g_blackbody_ramp_data[samples_read].b) == 3)
        {
            samples_read++;
        } else {
            fprintf(stderr, "\nWarning: Failed to parse line %d in ramp file: '%s'. Skipping.\n", samples_read + 1, line_buffer);
            // Optionally, could be made a fatal error
        }
    }

    fclose(file);

    if (samples_read != expected_samples) {
        fprintf(stderr, "\nError: Read %d samples, but expected %d from file '%s'.\n", samples_read, expected_samples, filename);
        free_blackbody_ramp(); // Free partially allocated/read data
        return false;
    }

    g_blackbody_ramp_size = samples_read;
    printf("OK (%d samples loaded).\n", g_blackbody_ramp_size);
    return true;
}

void free_blackbody_ramp(void) {
    if (g_blackbody_ramp_data) {
        free(g_blackbody_ramp_data);
        g_blackbody_ramp_data = NULL;
        g_blackbody_ramp_size = 0;
        printf("Freed blackbody ramp data.\n");
    }
}

double bb_log_temperature(double sqr_radius, double log_T0_isco) {
    if (sqr_radius <= 0) return -INFINITY;
    double A = log_T0_isco + LOGSHIFT;
    return A - 0.375 * log(sqr_radius);
}

// Intensity calculation is likely NO LONGER NEEDED if using normalized ramp
// Keep it commented out or remove if definitely unused.
/*
double bb_intensity(double temperature) {
    temperature = fmax(1.0, temperature);
    double exp_term = exp(INTENSITY_TEMP_CONST / temperature);
    if (isinf(exp_term) || exp_term <= 1.0) {
        return 0.0;
    }
    return 1.0 / (exp_term - 1.0);
}
*/

// Gets linear color from the loaded ramp data
ColorRGB bb_color_from_temp(double temperature) {
    if (!g_blackbody_ramp_data || g_blackbody_ramp_size == 0) {
        // fprintf(stderr, "Warning: Blackbody ramp not loaded, returning black for temp %.1fK.\n", temperature);
        // Printing this every time might spam the console, only print once or use logs.
        return COLOR_BLACK;
    }

    // Map temperature to normalized value [0, 1] within the ramp's range
    double temp_range = RAMP_TEMP_MAX - RAMP_TEMP_MIN;
    if (temp_range <= 0) return COLOR_BLACK; // Avoid division by zero

    double normalized_temp = (temperature - RAMP_TEMP_MIN) / temp_range;

    // Clamp normalized temp to [0, 1] range
    normalized_temp = fmax(0.0, fmin(1.0, normalized_temp));

    // Calculate index using nearest neighbor (simple lookup)
    // Subtract a tiny epsilon before casting to int to mimic Python's clip/astype behavior better
    // This prevents normalized_temp = 1.0 mapping to index = ramp_size
    int index = (int)(normalized_temp * (g_blackbody_ramp_size - 1e-9));

    // Clamp index just in case of floating point weirdness
    index = fmax(0, fmin(g_blackbody_ramp_size - 1, index));

    // Return the pre-calculated linear color from the ramp
    return g_blackbody_ramp_data[index];

    // --- Optional: Linear Interpolation ---
    // For smoother gradients, especially with fewer samples. With 2048, might be overkill.
    /*
    double float_index = normalized_temp * (g_blackbody_ramp_size - 1.0);
    int index0 = (int)float_index;
    int index1 = fmin(g_blackbody_ramp_size - 1, index0 + 1); // Ensure index1 is valid
    double lerp_factor = float_index - index0;

    ColorRGB color0 = g_blackbody_ramp_data[index0];
    ColorRGB color1 = g_blackbody_ramp_data[index1];

    // Interpolate each component
    ColorRGB result;
    result.r = color0.r + (color1.r - color0.r) * lerp_factor;
    result.g = color0.g + (color1.g - color0.g) * lerp_factor;
    result.b = color0.b + (color1.b - color0.b) * lerp_factor;
    return result;
    */
    // --- End Optional Interpolation ---
}

