#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h> // For EXIT_SUCCESS
#include <string.h>
#include <math.h>
#include "config.h"
#include "tracer.h"
#include "image.h"
#include "blackbody.h"
#include "bloom.h"
#include "color.h"


// --- Placeholder for run_tracer ---
bool run_tracer(Config *config, ImageF *output_image);

// Dummy implementation (replace with real one later in tracer.c)
bool run_tracer(Config *config, ImageF *output_image) {
    printf("--- Dummy run_tracer called ---\n");
    if (!config || !output_image || !output_image->pixels) return false;
    // Fill image with a simple gradient or color for testing postproc
    int W = output_image->width;
    int H = output_image->height;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
             double u = (double)x / W;
             double v = (double)y / H;
             output_image->pixels[y * W + x] = (ColorRGB){u, v, 0.5}; // Example gradient
        }
    }
    printf("--- Dummy run_tracer finished ---\n");
    return true;
}


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

    // --- 1b. Load Global Resources (like color ramp) ---
    // Assuming ramp path is fixed or determined during config load
    const char* ramp_path = "data/colourtemp.jpg"; // Make this configurable?
    printf("Loading color temperature ramp from %s...\n", ramp_path);
    if (!load_color_ramp(ramp_path)) {
         fprintf(stderr, "Failed to load essential color ramp.\n");
         free_config_textures(&config);
         return EXIT_FAILURE;
    }

    // --- 2. Create Output Image Buffer ---
    printf("Creating output image buffer (%dx%d)...\n", config.resolution[0], config.resolution[1]);
    ImageF* output_image = create_imagef(config.resolution[0], config.resolution[1]);
    if (!output_image) {
        fprintf(stderr, "Failed to create output image buffer.\n");
        free_config_textures(&config);
        free_color_ramp();
        return EXIT_FAILURE;
    }
    // Define W and H *after* output_image is created
    int W = output_image->width;
    int H = output_image->height;

    // --- 3. Run Ray Tracer ---
    printf("Running ray tracer...\n");
    bool trace_success = run_tracer(&config, output_image); // Now calls the dummy function
    if (!trace_success) {
        fprintf(stderr, "Ray tracing failed.\n");
        free_imagef(output_image);
        free_config_textures(&config);
        free_color_ramp();
        return EXIT_FAILURE;
    }

    // Create a temporary image buffer for intermediate results if needed
    printf("Creating post-processing buffer (%dx%d)...\n", W, H);
    ImageF* postproc_buffer = create_imagef(W, H);
    if (!postproc_buffer) {
        fprintf(stderr, "Failed to create post-processing buffer.\n");
        free_imagef(output_image);
        free_config_textures(&config);
        free_color_ramp();
        return EXIT_FAILURE;
    }

    // --- 4. Post Processing (Placeholder) ---
    printf("Applying post-processing (placeholder)...\n");
    // apply_postprocessing(&config, output_image);

    // --- 4a. Gain ---
    printf("Applying gain: %f\n", config.gain);
    for (int i = 0; i < output_image->width * output_image->height; ++i) {
        output_image->pixels[i] = color_mul_scalar(output_image->pixels[i], config.gain);
    }

    ImageF *current_image = output_image; // Start with the traced image
    ImageF *next_image = postproc_buffer; // Use buffer for next step

    // --- 4b. Airy Bloom ---
    if (config.airy_bloom) {
        printf("Applying Airy Bloom...\n");
        double spectrum[3] = {1.0, 0.86, 0.61};
        // Use atan2(1.0, 1.0/config.tan_fov) if tan_fov is tan(angle/2)?
        // Or just atan(config.tan_fov) if it's directly the tan value needed?
        // Python code uses np.arctan(TANFOV) - this is likely an error there,
        // as TANFOV is already a tangent-like value (1.5). atan(1.5) is ~0.98 rad (~56 deg).
        // If TANFOV was meant to be FoV in radians, then the formula makes sense.
        // Let's assume TANFOV=1.5 *is* the FoV in radians for now, matching Python's atan use.
        double fov_rad = config.tan_fov; // Assume config.tan_fov is FoV in radians
        // Check for zero FoV to prevent division by zero
         if (fabs(fov_rad) < 1e-6) {
            fprintf(stderr, "Warning: Field of View (tan_fov) is near zero. Using default scale for Airy Bloom.\n");
            fov_rad = 1.5; // Use a default value
        }
        double rad_scale = 0.00019825 * (double)W / fov_rad; // Use W (width) consistent with Python RESOLUTION[0]
        rad_scale *= config.airy_radius;

        double kernel_scale[3];
        kernel_scale[0] = rad_scale * spectrum[0];
        kernel_scale[1] = rad_scale * spectrum[1];
        kernel_scale[2] = rad_scale * spectrum[2];

        double max_intensity = 0.0;
        for (int i = 0; i < W * H; ++i) { // Use W * H
             max_intensity = fmax(max_intensity, fmax(current_image->pixels[i].r, fmax(current_image->pixels[i].g, current_image->pixels[i].b)));
        }

        int kernel_size_radius = (int)(25.0 * pow(fmax(0.1, max_intensity) / 5.0, 1.0/3.0) * (double)W / 1920.0); // Use W
        kernel_size_radius = fmax(1, kernel_size_radius);
        kernel_size_radius = fmin(100, kernel_size_radius);

        printf("  Max Intensity: %f\n", max_intensity);
        printf("  Calculated Kernel Radius (size): %d\n", kernel_size_radius);
        printf("  Kernel Scales (R,G,B): %f, %f, %f\n", kernel_scale[0], kernel_scale[1], kernel_scale[2]);


        Kernel* airy_kernel = generate_airy_kernel(kernel_scale, kernel_size_radius);
        if (airy_kernel) {
            printf("  Convolving image with Airy kernel (%dx%d)...\n", airy_kernel->width, airy_kernel->height);
            // Use W and H which are now defined in this scope
            if (!convolve2d_rgb(current_image, next_image, airy_kernel)) {
                fprintf(stderr, "Warning: Airy convolution failed.\n");
                memcpy(next_image->pixels, current_image->pixels, (size_t)W * H * sizeof(ColorRGB)); // Use W, H, cast size
            }
            free_kernel(airy_kernel);

            ImageF *tmp = current_image;
            current_image = next_image;
            next_image = tmp;

        } else {
            fprintf(stderr, "Warning: Failed to generate Airy kernel. Skipping bloom.\n");
            if (current_image != next_image) {
                memcpy(next_image->pixels, current_image->pixels, (size_t)W * H * sizeof(ColorRGB)); // Use W, H, cast size
                ImageF *tmp = current_image;
                current_image = next_image;
                next_image = tmp;
            }
        }
    } else {
        printf("Skipping Airy Bloom.\n");
        if (current_image != next_image) {
            memcpy(next_image->pixels, current_image->pixels, (size_t)W * H * sizeof(ColorRGB)); // Use W, H, cast size
            ImageF *tmp = current_image;
            current_image = next_image;
            next_image = tmp;
        }
    }

    // --- 4c. Gaussian Blur (Placeholder) ---
    if (config.blur_do) {
         printf("Applying Gaussian Blur (placeholder)...\n");
         // If implementing blur, copy current_image to next_image or apply in place if possible
         // For now, just ensure buffers are consistent
        if (current_image != next_image) { // This case shouldn't happen if bloom ran correctly, but good safety
            memcpy(next_image->pixels, current_image->pixels, (size_t)W * H * sizeof(ColorRGB)); // Use W, H, cast size
            ImageF *tmp = current_image;
            current_image = next_image;
            next_image = tmp;
        } else {
            // If blur modifies in place, current_image remains correct.
            // If blur needs separate buffers, copy output_image (or previous step) -> postproc_buffer
            // and then perform blur postproc_buffer -> output_image (or vice versa).
            // Let's assume blur would use the swap buffers:
            printf("  (Blur would use swap buffers: current -> next)\n");
            // Example: gaussian_blur(current_image, next_image, sigma);
            // ImageF *tmp = current_image; current_image = next_image; next_image = tmp; // Swap after blur
        }
    } else {
        printf("Skipping Gaussian Blur.\n");
        // Ensure final image buffer is correct; no swap needed if blur skipped.
    }

    // --- 4d. Normalization ---
     if (config.normalize > 0) {
        printf("Applying Normalization (target max %f)...\n", config.normalize);
        double current_max = 0.0;
        for (int i = 0; i < W * H; ++i) { // Use W * H
            current_max = fmax(current_max, fmax(current_image->pixels[i].r, fmax(current_image->pixels[i].g, current_image->pixels[i].b)));
        }
        printf("  Current max intensity: %f\n", current_max);
        if (current_max > 1e-6) {
            double norm_factor = config.normalize / current_max;
            printf("  Normalization factor: %f\n", norm_factor);
            for (int i = 0; i < W * H; ++i) { // Use W * H
                current_image->pixels[i] = color_mul_scalar(current_image->pixels[i], norm_factor);
            }
        } else {
            printf("  Skipping normalization as max intensity is near zero.\n");
        }
    }

    // --- Final Image Preparation ---
    ImageF* final_image = NULL;
    if (current_image == output_image) {
        final_image = output_image;
        printf("Final image is in original buffer.\n");
    } else {
        // This means the last operation wrote to postproc_buffer (which is next_image pointer here)
        // Copy the result from postproc_buffer back to the main output_image buffer
        printf("Final image is in postproc buffer. Copying back...\n");
        memcpy(output_image->pixels, current_image->pixels, (size_t)W * H * sizeof(ColorRGB)); // Use W, H, cast size
        final_image = output_image;
    }

    // --- 5. Save Image ---
    printf("Saving final image to tests/out_c.png...\n");

    // Create tests directory if it doesn't exist
    struct stat st = {0};
    if (stat("tests", &st) == -1) { // Check if directory exists
        printf("  'tests' directory not found, attempting to create...\n");
        #ifdef _WIN32
            if (_mkdir("tests") != 0) {
                perror("  Error creating 'tests' directory");
                // Decide if this is fatal or just a warning
            } else {
                printf("  'tests' directory created.\n");
            }
        #else
            if (mkdir("tests", 0775) != 0 && errno != EEXIST) { // Create with rwxrwx-r-x permissions, ignore error if it already exists
                perror("  Error creating 'tests' directory");
                // Decide if this is fatal or just a warning
            } else {
                if (errno == EEXIST) printf("  'tests' directory already exists.\n");
                else printf("  'tests' directory created.\n");
                errno = 0; // Reset errno after successful check/creation
            }
        #endif
    } else {
        printf("  'tests' directory already exists.\n");
    }

    // Clamp final image pixels *before* saving
    for (int i = 0; i < W * H; ++i) { // Use W * H
        final_image->pixels[i] = color_clamp(final_image->pixels[i], 0.0, 1.0);
    }

    if (!save_image_png(final_image, "tests/out_c.png", config.srgb_out)) {
        fprintf(stderr, "Error saving final image!\n");
        // Consider not exiting here, maybe just warn?
    } else {
        printf("  Image saved successfully.\n");
    }

    // --- 6. Cleanup ---
    printf("Cleaning up resources...\n");
    free_imagef(postproc_buffer);
    free_imagef(output_image); // Moved here, free final image buffer
    free_config_textures(&config);
    free_color_ramp();

    printf("Black Hole Tracer - Finished Successfully.\n");
    return EXIT_SUCCESS;
}
