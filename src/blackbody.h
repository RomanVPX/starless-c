#ifndef BLACKBODY_H
#define BLACKBODY_H

#include "color.h"
#include "image.h" // For Texture type
#include <stdbool.h>

// Global variable for the color ramp texture (or pass it around)
extern Texture *color_temp_ramp;

// Function to load the color ramp texture
bool load_color_ramp(const char *filename);

// Function to free the color ramp texture
void free_color_ramp(void);

// Calculate log temperature based on squared radius (R^2) and log T0 at ISCO
// Corresponds to Python's disktemp
double bb_log_temperature(double sqr_radius, double log_T0_isco);

// Calculate relative intensity from absolute temperature (K)
// Corresponds to Python's intensity
double bb_intensity(double temperature);

// Lookup color from the ramp based on absolute temperature (K)
// Corresponds to Python's colour
ColorRGB bb_color_from_temp(double temperature);


#endif // BLACKBODY_H
