#include "platform.h"
#include "postprocess.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "bloom.h"
#include "color.h"
#include "core_constants.h"


bool apply_postprocessing(const Config *cfg, ImageF *output_image)
{
    if (!cfg || !output_image || !output_image->pixels)
    {
        fprintf(stderr, "! Error: Invalid arguments passed to apply_postprocessing.\n");
        return false;
    }

    int W = output_image->width;
    int H = output_image->height;
    int num_pixels = W * H;
    size_t buffer_size = (size_t)num_pixels * sizeof(ColorRGB);

    printf("Applying post-processing...\n");

    // --- Create Post-Processing Buffers ---
    printf("  Creating post-processing buffers (%dx%d)...\n", W, H);
    ImageF *postproc_buffer = create_imagef(W, H); // Temporary work buffer
    ImageF *pre_bloom_copy = create_imagef(W, H);  // Buffer to hold state before bloom (source for blur)

    if (!postproc_buffer)
    {
        fprintf(stderr, "Failed to create post-processing buffer.\n");
        return false;
    }

    if (!pre_bloom_copy)
    {
        fprintf(stderr, "Failed to create pre-bloom copy buffer.\n");
        free_imagef(postproc_buffer);
        return false;
    }

    // --- 1. Gain ---
    printf("  Applying gain: %f\n", cfg->gain);
    for (int i = 0; i < num_pixels; ++i)
    {
        output_image->pixels[i] = color_mul_scalar(output_image->pixels[i], cfg->gain);
    }

    // --- Store pre-bloom state for Gaussian blur source ---
    printf("  Storing pre-bloom image state...\n");
    memcpy(pre_bloom_copy->pixels, output_image->pixels, buffer_size);

    // Setup buffer pointers for subsequent steps
    ImageF *current_image = output_image; // Starts with post-gain image
    ImageF *next_image = postproc_buffer; // Temp buffer for output of next step

    // --- 2. Airy Bloom ---
    // Reads from current_image, writes to next_image
    if (cfg->airy_bloom)
    {
        printf("  Applying Airy Bloom...\n");
        double spectrum[3] = {SPECTRUM_R, SPECTRUM_G, SPECTRUM_B};
        double fov_rad = cfg->tan_fov; // tangent of field of view
        // Check for zero FoV to prevent division by zero
        if (fabs(fov_rad) < EPSILON_LOOSE)
        {
            fprintf(stderr, "    Warning: Field of View (tan_fov) is near zero. Using default scale for Airy Bloom.\n");
            fov_rad = cfg->tan_fov; // Use a default value (from config_defaults.h)
        }
        // the float constant is 1.22 * 650nm / (4 mm), the typical diffractive resolution
        // of the human eye for red light. It's in radians, so we rescale using field of view.
        double rad_scale = AIRY_RAD_SCALE * (double)W / atan(fov_rad); // Use W (width) consistent with Python RESOLUTION[0]
        rad_scale *= cfg->airy_radius;                               // Apply user-defined radius scaling

        double kernel_scale[3] = {rad_scale * spectrum[0], rad_scale * spectrum[1], rad_scale * spectrum[2]};

        if (apply_airy_bloom(current_image, next_image, kernel_scale))
        {
            // Swap buffers: result is now in current_image, next_image is free
            ImageF *tmp = current_image;
            current_image = next_image;
            next_image = tmp;
        }
        else
        {
            fprintf(stderr, "    Warning: Airy bloom failed. Skipping.\n");
        }
    }
    else { printf("  Airy Bloom is disabled, skipping.\n"); }

    // At this point, current_image holds the "post-bloom" result ('colour_pb' in Python)
    // next_image points to a free buffer (either output_image or postproc_buffer)
    // pre_bloom_copy holds the source for the Gaussian blur ('total_colour_buffer_preproc' after gain)

    // --- 3. Gaussian Blur ---
    if (cfg->blur_do)
    {
        printf("  Applying Gaussian Blur...\n");
        // Sigma calculation based on Python: int(0.05*RESOLUTION[0])
        double sigma = fmax(0.5, 0.05 * (double)W); // Ensure sigma is positive
        printf("    Gaussian Sigma: %.2f\n", sigma);
        // Kernel size is determined by sigma, but we can set a max size if needed
        Kernel1D *gauss_kernel = generate_gaussian_kernel_1d(sigma, 0);
        if (gauss_kernel)
        {
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
            if (!convolve1d_h_rgb(blur_source, h_pass_dest, gauss_kernel, cfg->n_threads))
            {
                fprintf(stderr, "    Warning: Horizontal Gaussian convolution failed.\n");
            }

            // Pass 2: Vertical (h_pass_dest -> v_pass_dest)
            // Input: next_image = H(B). Output: B = V(H(B)) = S2.
            printf("    Applying vertical Gaussian pass (Source: next_image, Dest: pre_bloom_copy)...\n");
            if (!convolve1d_v_rgb(h_pass_dest, v_pass_dest, gauss_kernel, cfg->n_threads))
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
            for (int i = 0; i < num_pixels; ++i)
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

    // --- 4. Normalization ---
    if (cfg->normalize > 0)
    {
        printf("  Applying Normalization (target max %f)...\n", cfg->normalize);
        double current_max = 0.0;
        for (int i = 0; i < num_pixels; ++i)
        {
            current_max = fmax(current_max, fmax(current_image->pixels[i].r, fmax(current_image->pixels[i].g, current_image->pixels[i].b)));
        }
        printf("    Current max intensity: %f\n", current_max);
        if (current_max > EPSILON_LOOSE)
        {
            double norm_factor = cfg->normalize / current_max;
            printf("    Normalization factor: %f\n", norm_factor);
            for (int i = 0; i < num_pixels; ++i)
            {
                current_image->pixels[i] = color_mul_scalar(current_image->pixels[i], norm_factor);
            }
        }
        else { printf("    Skipping normalization as max intensity is near zero.\n"); }
    }
    else { printf("  Normalization is disabled, skipping.\n"); }

    // --- Ensure result is in output_image ---
    if (current_image != output_image)
    {
        printf("  Final image is in postproc buffer. Copying back to output_image for consistency before saving...\n");
        memcpy(output_image->pixels, current_image->pixels, buffer_size);
    }
    else { printf("  Final image is in original buffer.\n"); }

    // --- 5. ACES Tonemapping ---
    printf("  Applying ACES tonemapping (exposure = %f)...\n", cfg->aces_exposure);
    for (int i = 0; i < num_pixels; ++i)
    {
        ColorRGB hdr = output_image->pixels[i];
        ColorRGB mapped = aces_fitted(color_mul_scalar(hdr, cfg->aces_exposure));
        output_image->pixels[i] = mapped;
    }

    // --- Cleanup internal buffers ---
    free_imagef(postproc_buffer);
    free_imagef(pre_bloom_copy);

    return true;
}
