#ifndef BLACKBODY_H
#define BLACKBODY_H

#include "color.h"
#include "config.h"
#include <stdbool.h>


/**
* @brief Loads the blackbody color ramp data from a text file.
*
* Reads a file, auto-detects the number of samples, allocates memory,
* and stores the ramp data and size via output parameters.
*
* @param filename Path to the .ramp text file.
* @param ramp_data_out Pointer to store the address of the allocated ramp data array.
* @param ramp_size_out Pointer to store the number of samples loaded.
* @return true if loading was successful, false otherwise.
*/
bool load_blackbody_ramp_from_file(const char *filename, ColorRGB **ramp_data_out, int *ramp_size_out);

// Global variable for the color ramp texture (or pass it around)
extern Texture *color_temp_ramp;

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
 * @brief Gets the linear RGB color corresponding to a blackbody temperature from the provided ramp data.
 *
 * @param cfg Pointer to the Config struct containing the loaded ramp data and size.
 * @param temperature Absolute temperature in Kelvin.
 * @return ColorRGB structure containing linear RGB values.
 *         Returns COLOR_BLACK if the ramp is not loaded or on error.
 */
ColorRGB bb_color_from_temp(const Config *cfg, double temperature);


#endif // BLACKBODY_H
