#if defined(_MSC_VER)
    #define _USE_MATH_DEFINES
#endif
#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h> // For EXIT_SUCCESS
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "bloom.h"
#include "color.h"
#include "config.h"
#include "core_constants.h"
#include "image.h"
#include "tracer.h"
#if defined(_WIN32)
    #include <direct.h>
    #include <io.h>
    #ifndef F_OK
        #define F_OK 0
    #endif
    #define ACCESS _access
    #define STRDUP _strdup
#else
    #include <unistd.h>
    #define ACCESS access
    #define STRDUP strdup
#endif


int main(int argc, char *argv[])
{
    printf("Starless-C Black Hole Tracer - Starting...\n");

    Config config; // Will hold all settings

    // --- 1. Load Configuration ---
    printf("Loading configuration...\n");
    if (!load_config(argc, argv, &config))
    {
        fprintf(stderr, "Failed to load configuration.\n");
        return EXIT_FAILURE;
    }

    // --- 2. Create Output Image Buffer ---
    printf("Creating output image buffer (%dx%d)...\n", config.resolution[0], config.resolution[1]);
    ImageF *output_image = create_imagef(config.resolution[0], config.resolution[1]);
    if (!output_image)
    {
        fprintf(stderr, "Failed to create output image buffer.\n");
        free_config_textures(&config);
        return EXIT_FAILURE;
    }
    // Define W and H *after* output_image is created
    int W = output_image->width;
    int H = output_image->height;

    // --- 3. Run Ray Tracer ---
    printf("Running ray tracer...\n");
    bool trace_success = run_tracer(&config, output_image);
    if (!trace_success)
    {
        fprintf(stderr, "Ray tracing failed.\n");
        free_imagef(output_image);
        free_config_textures(&config);
        return EXIT_FAILURE;
    }

    // --- 4. Post Processing ---
    printf("Applying post-processing...\n");

    // --- Create Post-Processing Buffers ---
    size_t buffer_size = (size_t)W * H * sizeof(ColorRGB); // Calculate size once

    // Create a temporary image buffer for intermediate results if needed
    printf("  Creating post-processing buffers (%dx%d)...\n", W, H);
    ImageF *postproc_buffer = create_imagef(W, H); // Temporary work buffer
    ImageF *pre_bloom_copy = create_imagef(W, H);  // Buffer to hold state before bloom (source for blur)

    // Check postproc_buffer first
    if (!postproc_buffer)
    {
        fprintf(stderr, "Failed to create post-processing buffer.\n");
        free_imagef(output_image);
        free_config_textures(&config);
        return EXIT_FAILURE;
    }

    // Check pre_bloom_copy next
    if (!pre_bloom_copy)
    {
        fprintf(stderr, "Failed to create pre-bloom copy buffer.\n");
        free_imagef(output_image);
        free_imagef(postproc_buffer);
        free_config_textures(&config);
        return EXIT_FAILURE;
    }

    // --- 4a. Gain ---
    printf("  Applying gain: %f\n", config.gain);
    for (int i = 0; i < output_image->width * output_image->height; ++i)
    {
        output_image->pixels[i] = color_mul_scalar(output_image->pixels[i], config.gain);
    }

    // --- Store pre-bloom state for Gaussian blur source ---
    printf("  Storing pre-bloom image state...\n");
    memcpy(pre_bloom_copy->pixels, output_image->pixels, buffer_size);

    // Setup buffer pointers for subsequent steps
    ImageF *current_image = output_image; // Starts with post-gain image
    ImageF *next_image = postproc_buffer; // Temp buffer for output of next step

    // --- 4b. Airy Bloom ---
    // Reads from current_image, writes to next_image
    if (config.airy_bloom)
    {
        printf("  Applying Airy Bloom...\n");
        double spectrum[3] = {SPECTRUM_R, SPECTRUM_G, SPECTRUM_B};
        double fov_rad = config.tan_fov; // tangent of field of view
        // Check for zero FoV to prevent division by zero
        if (fabs(fov_rad) < EPSILON_LOOSE)
        {
            fprintf(stderr, "    Warning: Field of View (tan_fov) is near zero. Using default scale for Airy Bloom.\n");
            fov_rad = config.tan_fov; // Use a default value (from config_defaults.h)
        }
        // the float constant is 1.22 * 650nm / (4 mm), the typical diffractive resolution
        // of the human eye for red light. It's in radians, so we rescale using field of view.
        double rad_scale = AIRY_RAD_SCALE * (double)W / atan(fov_rad); // Use W (width) consistent with Python RESOLUTION[0]
        rad_scale *= config.airy_radius;                               // Apply user-defined radius scaling

        double kernel_scale[3] = {rad_scale * spectrum[0], rad_scale * spectrum[1], rad_scale * spectrum[2]};

        double max_intensity = 0.0;
        for (int i = 0; i < W * H; ++i)
        {
            max_intensity =
                    fmax(max_intensity, fmax(current_image->pixels[i].r, fmax(current_image->pixels[i].g, current_image->pixels[i].b)));
        }

        int kernel_size_radius = (int)(25.0 * pow(fmax(0.1, max_intensity) / 5.0, 1.0 / 3.0) * (double)W / 1920.0); // Use W
        kernel_size_radius = fmax(1, kernel_size_radius);
        kernel_size_radius = fmin(100, kernel_size_radius);

        printf("    Max Intensity: %f\n", max_intensity);
        printf("    Calculated Kernel Radius (size): %d\n", kernel_size_radius);
        printf("    Kernel Scales (R,G,B): %f, %f, %f\n", kernel_scale[0], kernel_scale[1], kernel_scale[2]);

        Kernel2D *airy_kernel = generate_airy_kernel(kernel_scale, kernel_size_radius);
        if (airy_kernel)
        {
            printf("    Convolving image with Airy kernel (%dx%d)...\n", airy_kernel->width, airy_kernel->height);
            // Use W and H which are now defined in this scope
            if (!convolve2d_rgb(current_image, next_image, airy_kernel))
            {
                fprintf(stderr, "    Warning: Airy convolution failed.\n");
                memcpy(next_image->pixels, current_image->pixels, buffer_size); // Use W, H, cast size
            }
            free_kernel2d(airy_kernel);

            // Swap buffers: result is now in current_image, next_image is free
            ImageF *tmp = current_image;
            current_image = next_image;
            next_image = tmp;
        }
        else
        {
            fprintf(stderr, "    Warning: Failed to generate Airy kernel. Skipping bloom.\n");
            printf("    (Post-bloom result remains in current_image buffer)\n");
        }
    }
    else { printf("  Airy Bloom is disabled, skipping.\n"); }

    // At this point, current_image holds the "post-bloom" result ('colour_pb' in Python)
    // next_image points to a free buffer (either output_image or postproc_buffer)
    // pre_bloom_copy holds the source for the Gaussian blur ('total_colour_buffer_preproc' after gain)

    // --- 4c. Gaussian Blur ---
    if (config.blur_do)
    {
        printf("  Applying Gaussian Blur...\n");
        // Sigma calculation based on Python: int(0.05*RESOLUTION[0])
        double sigma = fmax(0.5, 0.05 * (double)W); // Ensure sigma is positive
        printf("    Gaussian Sigma: %.2f\n", sigma);
        // Kernel size is determined by sigma, but we can set a max size if needed
        Kernel1D *gauss_kernel = generate_gaussian_kernel_1d(sigma, 0);
        if (gauss_kernel)
        {
            // --- Third Buffer Plan Implementation ---
            // Buffers: A=output_image, B=pre_bloom_copy, C=postproc_buffer
            // State before this block:
            //   current_image points to buffer holding S1 (post-bloom result).
            //   next_image points to a free buffer.
            //   pre_bloom_copy (B) holds S0 (post-gain result), used as SOURCE for H pass.
            ImageF *blur_source = pre_bloom_copy; // B = S0
            ImageF *h_pass_dest = next_image;     // Use the currently free buffer for H(B)
            ImageF *v_pass_dest = pre_bloom_copy; // Reuse B for the final blur result S2 = V(H(B))

            // Pass 1: Horizontal (blur_source -> h_pass_dest)
            // Input: B=S0. Output: next_image = H(B).
            printf("    Applying horizontal Gaussian pass (Source: pre_bloom_copy, Dest: next_image)...\n");
            if (!convolve1d_h_rgb(blur_source, h_pass_dest, gauss_kernel))
            {
                fprintf(stderr, "    Warning: Horizontal Gaussian convolution failed.\n");
            }

            // Pass 2: Vertical (h_pass_dest -> v_pass_dest)
            // Input: next_image = H(B). Output: B = V(H(B)) = S2.
            printf("    Applying vertical Gaussian pass (Source: next_image, Dest: pre_bloom_copy)...\n");
            if (!convolve1d_v_rgb(h_pass_dest, v_pass_dest, gauss_kernel))
            {
                fprintf(stderr, "    Warning: Vertical Gaussian convolution failed.\n");
            }
            // current_image holds S1 (post-bloom result).
            // pre_bloom_copy (v_pass_dest / B) holds S2 (final blur result).
            // next_image (h_pass_dest) holds H(B) (intermediate, no longer needed).

            ImageF *blurred_image_final = v_pass_dest; // S2 is in pre_bloom_copy buffer

            // Additive Step: current_image = current_image + 0.2 * blurred_image_final
            // Input: current_image (S1), blurred_image_final (S2). Output: current_image = S1 + 0.2*S2
            printf("    Adding 0.2 * blurred image to post-bloom result...\n");
            for (int i = 0; i < W * H; ++i)
            {
                ColorRGB blur_contrib = color_mul_scalar(blurred_image_final->pixels[i], 0.2);
                current_image->pixels[i] = color_add(current_image->pixels[i], blur_contrib);
            }
            // The final result (post-bloom + 0.2 * blur) is in current_image.
            // next_image holds the blur result (no longer needed directly).
            free_kernel1d(gauss_kernel);
        }
        else
        {
            fprintf(stderr, "  Warning: Failed to generate Gaussian kernel. Skipping blur.\n");
            // If skipping, current_image still holds the post-bloom result.
        }
    }
    else
    {
        printf("  Gaussian Blur is disabled, skipping.\n");
        // No changes to current_image or next_image needed.
    }
    // At this point, current_image holds the final result before normalization.

    // --- 4d. Normalization ---
    if (config.normalize > 0)
    {
        printf("  Applying Normalization (target max %f)...\n", config.normalize);
        double current_max = 0.0;
        for (int i = 0; i < W * H; ++i)
        { // Use W * H
            current_max = fmax(current_max, fmax(current_image->pixels[i].r, fmax(current_image->pixels[i].g, current_image->pixels[i].b)));
        }
        printf("    Current max intensity: %f\n", current_max);
        if (current_max > EPSILON_LOOSE)
        {
            double norm_factor = config.normalize / current_max;
            printf("    Normalization factor: %f\n", norm_factor);
            for (int i = 0; i < W * H; ++i)
            { // Use W * H
                current_image->pixels[i] = color_mul_scalar(current_image->pixels[i], norm_factor);
            }
        }
        else { printf("    Skipping normalization as max intensity is near zero.\n"); }
    }
    else { printf("  Normalization is disabled, skipping.\n"); }

    // --- Final Image Preparation ---
    ImageF *final_image = NULL;
    if (current_image == output_image)
    {
        final_image = output_image;
        printf("  Final image is in original buffer.\n");
    }
    else
    {
        // current_image must be postproc_buffer
        final_image = postproc_buffer;                                  // Point to the buffer holding result
        printf("  Final image is in postproc buffer. Copying back to output_image for consistency before saving...\n");
        memcpy(output_image->pixels, final_image->pixels, buffer_size); // Copy result to output_image
        final_image = output_image;                                     // Now final result is definitively in output_image
    }

    // --- 4e. ACES Tonemapping ---
    printf("  Applying ACES tonemapping (exposure = %f)...\n", config.aces_exposure);
    for (int i = 0; i < W * H; ++i)
    {
        ColorRGB hdr = final_image->pixels[i];
        ColorRGB mapped = aces_fitted(color_mul_scalar(hdr, config.aces_exposure));
        final_image->pixels[i] = mapped;
    }

    // --- 5. Save Image ---
    printf("Saving final image...\n");
    const char *base_name_for_output;
    if (config.scene_base_name && strlen(config.scene_base_name) > 0) { base_name_for_output = config.scene_base_name; }
    else { base_name_for_output = "output"; }

    char *base_name_copy = STRDUP(base_name_for_output);
    if (!base_name_copy)
    {
        fprintf(stderr, "! Error: Failed to allocate memory for base name copy.\n");
        free_imagef(postproc_buffer);
        free_imagef(pre_bloom_copy);
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

    // Clamp final image pixels *before* saving
    for (int i = 0; i < W * H; ++i)
    { // Use W * H
        final_image->pixels[i] = color_clamp(final_image->pixels[i], 0.0, 1.0);
    }

    if (!save_image_png(final_image, output_path, config.srgb_out, &config)) { fprintf(stderr, "! Error saving final image!\n"); }
    else { printf("  Image saved successfully.\n"); }

    // --- 6. Cleanup ---
    printf("Cleaning up resources...\n");
    free_imagef(postproc_buffer); // Free the temporary buffer
    free_imagef(pre_bloom_copy);  // Free the pre-bloom copy buffer
    free_imagef(output_image);    // Free the main image buffer
    free_config_textures(&config);

    printf("Black Hole Tracer - Finished Successfully.\n");
    return EXIT_SUCCESS;
}
