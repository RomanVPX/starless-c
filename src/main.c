#include "platform.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h> // For EXIT_SUCCESS
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "color.h"
#include "config.h"
#include "image.h"
#include "postprocess.h"
#include "tracer.h"


int main(int argc, char *argv[])
{
    printf("Starless-C Black Hole Tracer - Starting...\n");

    Config config; // Will hold all settings

    // --- Load Configuration ---
    printf("Loading configuration...\n");
    if (!load_config(argc, argv, &config))
    {
        fprintf(stderr, "Failed to load configuration.\n");
        return EXIT_FAILURE;
    }

    // --- Create Output Image Buffer ---
    printf("Creating output image buffer (%dx%d)...\n", config.resolution[0], config.resolution[1]);
    ImageF *output_image = create_imagef(config.resolution[0], config.resolution[1]);
    if (!output_image)
    {
        fprintf(stderr, "Failed to create output image buffer.\n");
        free_config_textures(&config);
        return EXIT_FAILURE;
    }

    // --- Run Ray Tracer ---
    printf("Running ray tracer...\n");
    bool trace_success = run_tracer(&config, output_image);
    if (!trace_success)
    {
        fprintf(stderr, "Ray tracing failed.\n");
        free_imagef(output_image);
        free_config_textures(&config);
        return EXIT_FAILURE;
    }

    // --- Post Processing ---
    if (!apply_postprocessing(&config, output_image))
    {
        fprintf(stderr, "Post-processing failed.\n");
        free_imagef(output_image);
        free_config_textures(&config);
        return EXIT_FAILURE;
    }

    // --- Save Image ---
    printf("Saving final image...\n");
    const char *base_name_for_output;
    if (config.scene_base_name && strlen(config.scene_base_name) > 0) { base_name_for_output = config.scene_base_name; }
    else { base_name_for_output = "output"; }

    char *base_name_copy = STRDUP(base_name_for_output);
    if (!base_name_copy)
    {
        fprintf(stderr, "! Error: Failed to allocate memory for base name copy.\n");
        free_imagef(output_image);
        free_config_textures(&config);
        return EXIT_FAILURE;
    }

    // Create output directory
    struct stat st = {0};
    if (stat("out", &st) == -1)
    {
#if defined(_WIN32)
        if (_mkdir("out") != 0)
#else
        if (mkdir("out", 0775) != 0 && errno != EEXIST)
#endif
        {
            perror("! Error creating 'out' directory");
            free(base_name_copy);
            return EXIT_FAILURE;
        }
    }

    // Find first available file name
    char output_path[256];
    int index = 0;
    do {
        snprintf(output_path, sizeof(output_path), "out/%s_%s_%03d.png", base_name_copy, config.lofi ? "(L)" : "(H)", index++);
    } while (ACCESS(output_path, F_OK) != -1 && index < 1000);

    if (index >= 1000)
    {
        fprintf(stderr, "! Error: Too many output files for scene %s\n", base_name_copy);
        free(base_name_copy);
        return EXIT_FAILURE;
    }

    printf("  Saving final image to %s...\n", output_path);
    free(base_name_copy);

    // Clamp final image pixels before saving
    for (int i = 0; i < output_image->width * output_image->height; ++i)
    {
        output_image->pixels[i] = color_clamp(output_image->pixels[i], 0.0, 1.0);
    }

    if (!save_image_png(output_image, output_path, config.srgb_out, &config)) { fprintf(stderr, "! Error saving final image!\n"); }
    else { printf("  Image saved successfully.\n"); }

    // --- Cleanup ---
    printf("Cleaning up resources...\n");
    free_imagef(output_image);    // Free the main image buffer
    free_config_textures(&config);

    printf("Black Hole Tracer - Finished Successfully.\n");
    return EXIT_SUCCESS;
}
