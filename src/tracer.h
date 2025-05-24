#ifndef TRACER_H
#define TRACER_H

#include "config.h" // Needs access to configuration
#include "image.h"  // Needs access to image buffer
#include <stdbool.h>

// Main entry point for the ray tracing process.
// This function will set up threads and manage the overall tracing.
// For now, it can just run single-threaded.
// Takes the configuration and the output image buffer (pre-allocated).
// Returns true on success, false on failure.
bool run_tracer(Config *config, ImageF *output_image);

// Structure to hold the state of a single ray during integration
typedef struct {
    Vec3d pos;          // Current position (r)
    Vec3d vel;          // Current velocity (dr/d_lambda)
    Vec3d initial_vel;  // Initial normalized view vector (for sky lookup)
    double h2;          // Squared specific angular momentum (constant of motion)

    ColorRGB color;     // Accumulated color
    double alpha;       // Accumulated alpha

    bool active;        // Is the ray still being traced? (Not hit horizon/escaped)
    int steps_taken;    // Counter for iterations
    // Add other state if needed (e.g., distance traveled)

} RayState;


// Structure to hold thread-specific data
typedef struct {
    int thread_id;
    unsigned int rand_seed; // Random seed for this thread (for jittering)
    Config *config;
    ImageF *image;
    int start_pixel_index;
    int end_pixel_index; // Exclusive: trace pixels [start, end)
    // Add shared resources if necessary (e.g., progress counter mutex/atomic)
} ThreadData;


#endif // TRACER_H
