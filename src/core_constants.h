#ifndef STARLESS_CORE_CONSTANTS_H
#define STARLESS_CORE_CONSTANTS_H

// Physical and algorithmic constants of the Starless core
// Everything related to mathematics, physics, algorithms rather than scene/config

// --- Epsilons ---
#define EPSILON_STRICT 1e-9   // Used in airy_disk_func, kernel generation, normalizations
#define EPSILON_LOOSE  1e-6   // Used for "almost zero" checks elsewhere

// --- Physical coefficients ---
// Logarithmic shift for temperature (see original Starless, blackbody.c)
#define LOGSHIFT 0.823959216501

// Minimum temperature for blackbody ramp (K)
#define RAMP_TEMP_MIN 1000.0
// Maximum temperature for blackbody ramp (K)
#define RAMP_TEMP_MAX 50000.0
// Temperature low cutoff for blackbody visibility (K)
#define TEMP_CUTOFF_LOW 1000.0
// Temperature high cutoff for blackbody visibility (K)
#define TEMP_CUTOFF_HIGH 25000.0

// Exponent for temperature profile T(r) ∝ r^{-3/8} in Shakura-Sunyaev disk model
#define SHAKURA_SUNYAEV_TEMP_EXP 0.375

// Scale for Airy bloom (see main.c, comment: "the float constant is 1.22 * 650nm / (4 mm)")
// 1.22 — diffraction limit, 650nm — red wavelength, 4mm — pupil diameter
#define AIRY_RAD_SCALE 0.00019825

// --- Spectral coefficients ---
// Used for modeling color dependence of Airy bloom
#define SPECTRUM_R 1.0
#define SPECTRUM_G 0.86
#define SPECTRUM_B 0.61

#endif // STARLESS_CORE_CONSTANTS_H
