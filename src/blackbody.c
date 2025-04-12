#include "blackbody.h"
#include "color.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Module Constants ---
static const double LOGSHIFT = 0.823959216501;
static const double RAMP_TEMP_MIN = 1000.0;
static const double RAMP_TEMP_MAX = 30000.0;


// --- Function to count valid data lines in a file ---
static int count_ramp_samples(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        return -1;
    }
    int count = 0;
    char line[256];
    double r, g, b;
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '\n' || line[0] == '#' || line[0] == '\0') continue;
        if (sscanf(line, "%lf %lf %lf", &r, &g, &b) == 3) {
            count++;
        }
    }
    fclose(file);
    return count;
}

bool load_blackbody_ramp_from_file(const char *filename, ColorRGB **ramp_data_out, int *ramp_size_out) {
    *ramp_data_out = NULL;
    *ramp_size_out = 0;

    int samples_to_load = count_ramp_samples(filename);
    if (samples_to_load <= 0) {
        fprintf(stderr, "Error: Failed to determine sample count in '%s' or file is empty/invalid.\n", filename);
        return false;
    }

    fflush(stdout);

    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening ramp file");
        return false;
    }

    // Allocate memory
    ColorRGB *loaded_data = (ColorRGB *)malloc(samples_to_load * sizeof(ColorRGB));
    if (!loaded_data) {
        fprintf(stderr, "\nError: Failed to allocate memory for blackbody ramp (%d samples).\n", samples_to_load);
        fclose(file);
        return false;
    }

    char line_buffer[256];
    int samples_read = 0;
    while (samples_read < samples_to_load && fgets(line_buffer, sizeof(line_buffer), file)) {
         // Skip empty lines or comment lines again during actual read
        if (line_buffer[0] == '\n' || line_buffer[0] == '#' || line_buffer[0] == '\0') continue;

        if (sscanf(line_buffer, "%lf %lf %lf",
                   &loaded_data[samples_read].r,
                   &loaded_data[samples_read].g,
                   &loaded_data[samples_read].b) == 3)
        {
            // Optional: Validate loaded data here (NaN, negative checks)
             ColorRGB *color = &loaded_data[samples_read];
             if (isnan(color->r) || isnan(color->g) || isnan(color->b) ||
                 color->r < 0.0 || color->g < 0.0 || color->b < 0.0) {
                 fprintf(stderr, "\nWarning: Invalid color value at sample %d in '%s'. Clamping to >= 0.\n",
                         samples_read, filename);
                 color->r = fmax(0.0, color->r);
                 color->g = fmax(0.0, color->g);
                 color->b = fmax(0.0, color->b);
             }
            samples_read++;
        } else {
            fprintf(stderr, "\nWarning: Failed to parse line %d (approx) in ramp file '%s'. Skipping.\n", samples_read + 1, filename);
        }
    }

    fclose(file);

    if (samples_read != samples_to_load) {
        fprintf(stderr, "\nError: Read %d samples, but expected %d based on initial count from file '%s'. File might have changed or is inconsistent.\n", samples_read, samples_to_load, filename);
        free(loaded_data); // Free partially allocated/read data
        return false;
    }

    *ramp_data_out = loaded_data;
    *ramp_size_out = samples_read;
    printf("OK (%d samples loaded).\n", *ramp_size_out);
    return true;
}

// Removed: free_blackbody_ramp()

// --- Temperature calculation function (no change) ---
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

// --- Color lookup function ---
ColorRGB bb_color_from_temp(const Config *cfg, double temperature) {
    // Access ramp data via cfg pointer
    if (!cfg || !cfg->blackbody_ramp_data || cfg->blackbody_ramp_size == 0) {
        // This check should ideally not be hit if loading is conditional,
        // but it's good defensive programming.
        fprintf(stderr, "Warning: Blackbody ramp data not available in config!\n");
        return COLOR_BLACK;
    }

    double temp_range = RAMP_TEMP_MAX - RAMP_TEMP_MIN;
    if (temp_range <= 0) return COLOR_BLACK;

    double normalized_temp = (temperature - RAMP_TEMP_MIN) / temp_range;
    normalized_temp = fmax(0.0, fmin(1.0, normalized_temp));

    int index = (int)(normalized_temp * (cfg->blackbody_ramp_size - 1e-9));
    index = fmax(0, fmin(cfg->blackbody_ramp_size - 1, index));

    return cfg->blackbody_ramp_data[index];

    // Optional Linear Interpolation using cfg->blackbody_ramp_data and cfg->blackbody_ramp_size
    /*
    double float_index = normalized_temp * (cfg->blackbody_ramp_size - 1.0);
    int index0 = (int)float_index;
    int index1 = fmin(cfg->blackbody_ramp_size - 1, index0 + 1);
    double lerp_factor = float_index - index0;
    ColorRGB color0 = cfg->blackbody_ramp_data[index0];
    ColorRGB color1 = cfg->blackbody_ramp_data[index1];
    // ... interpolation logic ...
    return result;
    */
}
