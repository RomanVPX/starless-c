#include <stdio.h>
#include <stdlib.h> // For EXIT_SUCCESS
#include "config.h"
#include "tracer.h"
#include "image.h"

// Forward declarations for functions we'll define later
// bool run_tracer(Config *config, ImageF *output_image); // Example

int main(int argc, char *argv[]) {
    printf("Black Hole Tracer (C Version) - Starting...\n");

    Config config; // Will hold all settings

    // --- 1. Load Configuration (Placeholder) ---
    // We'll replace this with actual loading soon
    printf("Loading configuration (placeholder)...\n");
    if (!load_config(argc, argv, &config)) {
         fprintf(stderr, "Failed to load configuration.\n");
         return EXIT_FAILURE;
     }

    // --- 2. Create Output Image Buffer ---
    printf("Creating output image buffer (%dx%d)...\n", config.resolution[0], config.resolution[1]);
    ImageF* output_image = create_imagef(config.resolution[0], config.resolution[1]);
    if (!output_image) {
        fprintf(stderr, "Failed to create output image buffer.\n");
        free_config_textures(&config); // Clean up textures if loaded
        return EXIT_FAILURE;
    }

    // --- 3. Run Ray Tracer (Placeholder) ---
    printf("Running ray tracer (placeholder)...\n");
    // bool success = run_tracer(&config, output_image); // Call the main tracing function here

    // --- 4. Post Processing (Placeholder) ---
     printf("Applying post-processing (placeholder)...\n");
     // apply_postprocessing(&config, output_image);


    // --- 5. Save Image (Placeholder) ---
    printf("Saving final image (placeholder)...\n");
    // save_image_png(output_image, "tests/out_c.png", config.srgb_out);

    // --- 6. Cleanup ---
    printf("Cleaning up resources...\n");
    free_imagef(output_image);
    free_config_textures(&config); // Free textures loaded in config

    printf("Black Hole Tracer - Finished Successfully (Placeholder).\n");
    return EXIT_SUCCESS;
}
