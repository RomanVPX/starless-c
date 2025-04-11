#ifndef BLACKBODY_H
#define BLACKBODY_H

#include "color.h"
#include "image.h" // For Texture type
#include <stdbool.h>


/**
* @brief Loads the blackbody color ramp data from a text file.
*
* Reads a file containing N lines, each with three float values (R G B)
* representing linear RGB colors. Allocates memory to store the ramp.
* Must be called before bb_color_from_temp.
*
* @param filename Path to the .ramp text file.
* @param expected_samples The number of color samples expected in the file (e.g., 2048).
* @return true if loading was successful, false otherwise.
*/
bool load_blackbody_ramp_from_file(const char *filename, int expected_samples);

/**
* @brief Frees the memory allocated for the blackbody color ramp.
* Should be called before program exit.
*/
void free_blackbody_ramp(void);

// Global variable for the color ramp texture (or pass it around)
extern Texture *color_temp_ramp;

// Function to load the color ramp texture
// bool load_color_ramp(const char *filename);

// Function to free the color ramp texture
// void free_color_ramp(void);

/**
* @brief Calculates the log (natural) temperature of the accretion disk at a given squared radius.
* @param sqr_radius Squared distance from the center.
* @param log_T0_isco Log (natural) of the temperature at the ISCO (reference temperature).
* @return Log (natural) of the temperature in Kelvin.
*/
double bb_log_temperature(double sqr_radius, double log_T0_isco);

/**
* @brief Calculates a relative intensity factor based on temperature.
* Approximates Planck's law integral.
* @param temperature Absolute temperature in Kelvin.
* @return Relative intensity factor (>= 0).
*/
// double bb_intensity(double temperature); // Keep this for now, might be useful later? Or remove if definitely not needed.

/**
* @brief Gets the linear RGB color corresponding to a blackbody temperature from the pre-loaded ramp.
* @param temperature Absolute temperature in Kelvin.
* @return ColorRGB structure containing linear RGB values (normalized relative to ramp max).
*         Returns COLOR_BLACK if the ramp is not loaded or on error.
*/
ColorRGB bb_color_from_temp(double temperature);


// Lookup color from the ramp based on absolute temperature (K)
// Corresponds to Python's colour
//ColorRGB bb_color_from_temp(double temperature); // TODO: REMOVE THIS IF NOT NEEDED


#endif // BLACKBODY_H
