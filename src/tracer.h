#ifndef TRACER_H
#define TRACER_H

#include <stdbool.h>
#include "config.h" // Needs access to configuration
#include "image.h"  // Needs access to image buffer

// Main entry point for the ray tracing process.
// Takes the configuration and the output image buffer (pre-allocated).
// Returns true on success, false on failure.
bool run_tracer(Config *config, ImageF *output_image);

// Structure to hold the state of a single ray during integration
typedef struct
{
    Vec3d pos;         // Current position (r)
    Vec3d vel;         // Current velocity (dr/d_lambda)
    double h2;         // Squared specific angular momentum (constant of motion)

    ColorRGB color;    // Accumulated color
    double alpha;      // Accumulated alpha

    bool active;       // Is the ray still being traced? (Not hit horizon/escaped)
    int steps_taken;   // Counter for iterations
} RayState;

#endif // TRACER_H
