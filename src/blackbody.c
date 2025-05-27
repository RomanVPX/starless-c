#include "blackbody.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "color.h"
#if defined(_WIN32)
    #define SSCANF sscanf_s
#else
    #define SSCANF sscanf
#endif


#define SHAKURA_SUNYAEV_TEMP_EXP 0.375          // Exponent for temperature profile T(r) ∝ r^{-3/8} in Shakura-Sunyaev disk model
#define LOGSHIFT                 0.823959216501 // Logarithmic shift for temperature (see original Starless, blackbody.c)

// Should match values in ramp file:
#define RAMP_TEMP_MIN            1000.0  // Minimum temperature for blackbody ramp (K)
#define RAMP_TEMP_MAX            50000.0 // Maximum temperature for blackbody ramp (K)


// --- Function to count valid data lines in a file ---
static int count_ramp_samples(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file) { return -1; }
    int count = 0;
    char line[512]; // It's 33 characters for RGB values, but allow some extra space for comments or whatever
    double r, g, b;
    while (fgets(line, sizeof(line), file))
    {
        if (line[0] == '\n' || line[0] == '#' || line[0] == '\0') { continue; }
        if (SSCANF(line, "%lf %lf %lf", &r, &g, &b) == 3) { count++; }
    }
    fclose(file);
    return count;
}


bool load_blackbody_ramp_from_file(const char *filename, ColorRGB **ramp_data_out, int *ramp_size_out)
{
    *ramp_data_out = NULL;
    *ramp_size_out = 0;

    int smp_to_load = count_ramp_samples(filename);
    if (smp_to_load <= 0)
    {
        fprintf(stderr, "! Error: Failed to determine sample count in '%s' or file is empty/invalid.\n", filename);
        return false;
    }

    fflush(stdout);

    FILE *file = fopen(filename, "r");
    if (!file)
    {
        perror("! Error opening ramp file");
        return false;
    }

    // Allocate memory
    ColorRGB *loaded_data = (ColorRGB *)malloc(smp_to_load * sizeof(ColorRGB));
    if (!loaded_data)
    {
        fprintf(stderr, "\n! Error: Failed to allocate memory for blackbody ramp (%d samples).\n", smp_to_load);
        fclose(file);
        return false;
    }

    char line_buffer[256];
    int smp_read = 0;
    while (smp_read < smp_to_load && fgets(line_buffer, sizeof(line_buffer), file))
    {
        // Skip empty lines or comment lines again during actual read
        if (line_buffer[0] == '\n' || line_buffer[0] == '#' || line_buffer[0] == '\0') continue;

        if (SSCANF(line_buffer, "%lf %lf %lf", &loaded_data[smp_read].r, &loaded_data[smp_read].g, &loaded_data[smp_read].b) == 3)
        {
            // Optional: Validate loaded data here (NaN, negative checks)
            ColorRGB *color = &loaded_data[smp_read];
            if (isnan(color->r) || isnan(color->g) || isnan(color->b) || color->r < 0.0 || color->g < 0.0 || color->b < 0.0)
            {
                fprintf(stderr, "\n  Warning: Invalid color value at sample %d in '%s'. Clamping to >= 0.\n", smp_read, filename);
                color->r = fmax(0.0, color->r);
                color->g = fmax(0.0, color->g);
                color->b = fmax(0.0, color->b);
            }
            smp_read++;
        }
        else { fprintf(stderr, "\n  Warning: Failed to parse line %d (approx) in ramp file '%s'. Skipping.\n", smp_read + 1, filename); }
    }

    fclose(file);

    if (smp_read != smp_to_load)
    {
        fprintf(stderr,
                "\n! Error: Read %d samples, but expected %d based on initial count from file '%s'. File might have changed or is "
                "inconsistent.\n",
                smp_read, smp_to_load, filename);
        free(loaded_data); // Free partially allocated/read data
        return false;
    }

    *ramp_data_out = loaded_data;
    *ramp_size_out = smp_read;
    printf("  OK (%d samples loaded).\n", *ramp_size_out);
    return true;
}


// --- Temperature calculation function (no change) ---
double bb_log_temperature(double sqr_radius, double log_T0_isco)
{
    if (sqr_radius <= 0) { return -INFINITY; }
    double A = log_T0_isco + LOGSHIFT;
    return A - SHAKURA_SUNYAEV_TEMP_EXP * log(sqr_radius);
}


// --- Color lookup function ---
ColorRGB bb_color_from_temp(const Config *cfg, double temperature)
{
    // Access ramp data via cfg pointer
    if (!cfg || !cfg->blackbody_ramp_data || cfg->blackbody_ramp_size == 0)
    {
        // This check should ideally not be hit if loading is conditional,
        // but it's good defensive programming.
        fprintf(stderr, "Warning: Blackbody ramp data not available in config!\n");
        return COLOR_BLACK;
    }

    double temp_range = RAMP_TEMP_MAX - RAMP_TEMP_MIN;
    if (temp_range <= 0) return COLOR_BLACK;

    double normalized_temp = (temperature - RAMP_TEMP_MIN) / temp_range;
    normalized_temp = fmax(0.0, fmin(1.0, normalized_temp));

    int index = (int)(normalized_temp * (cfg->blackbody_ramp_size - 1));
    index = fmax(0, fmin(cfg->blackbody_ramp_size - 1, index));

    return cfg->blackbody_ramp_data[index];
}
