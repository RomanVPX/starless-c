#include "bloom.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // For memset
#include "core_constants.h"


// --- Airy Disk Function ---
// Approximates (2*J1(x)/x)^2
// Handles the limit at x=0 where the value should be 1.0
static double airy_disk_func(double x)
{
    if (fabs(x) < EPSILON_STRICT) { return 1.0; }
#if defined(_WIN32) && !defined(__CYGWIN__)
    double bessel_j1 = _j1(x);
#else
    double bessel_j1 = j1(x);
#endif
    double val = 2.0 * bessel_j1 / x;
    return val * val;
}


// --- Generate 2D Kernel ---
Kernel2D *generate_airy_kernel(const double scale[3], int size)
{
    if (size < 0)
    {
        fprintf(stderr, "Error: Kernel size cannot be negative.\n");
        return NULL;
    }

    Kernel2D *k = (Kernel2D *)malloc(sizeof(Kernel2D));
    if (!k)
    {
        fprintf(stderr, "Error: Failed to allocate memory for Kernel struct.\n");
        return NULL;
    }

    k->size = size;
    k->width = 2 * size + 1;
    k->height = 2 * size + 1;
    k->data = (ColorRGB *)malloc(k->width * k->height * sizeof(ColorRGB));
    if (!k->data)
    {
        fprintf(stderr, "Error: Failed to allocate memory for kernel data (%dx%d).\n", k->width, k->height);
        free(k);
        return NULL;
    }
    memset(k->data, 0, k->width * k->height * sizeof(ColorRGB)); // Initialize

    double sum[3] = {0.0, 0.0, 0.0};                             // For normalization

    for (int j = -size; j <= size; ++j)
    {
        for (int i = -size; i <= size; ++i)
        {
            double r = sqrt((double)(i * i + j * j)) + EPSILON_STRICT;
            int kernel_idx = (j + size) * k->width + (i + size);

            double val_r = airy_disk_func(r / scale[0]);
            double val_g = airy_disk_func(r / scale[1]);
            double val_b = airy_disk_func(r / scale[2]);

            if (val_r < EPSILON_LOOSE && val_g < EPSILON_LOOSE && val_b < EPSILON_LOOSE)
            {
                k->data[kernel_idx] = (ColorRGB){0.0, 0.0, 0.0};
                continue;
            }

            k->data[kernel_idx] = (ColorRGB){val_r, val_g, val_b};
            sum[0] += val_r;
            sum[1] += val_g;
            sum[2] += val_b;
        }
    }


    // --- Normalize Kernel ---
    // Prevent division by zero if sum is zero (e.g., size=0 and scale results in NaN/Inf)
    for (int c = 0; c < 3; ++c)
    {
        if (fabs(sum[c]) < EPSILON_STRICT)
        {
            fprintf(stderr, "Warning: Kernel sum for channel %d is close to zero. Normalization skipped for this channel.\n", c);
            sum[c] = 1.0; // Avoid division by zero, result will be unnormalized (likely all zero anyway)
        }
    }

    for (int idx = 0; idx < k->width * k->height; ++idx)
    {
        k->data[idx].r /= sum[0];
        k->data[idx].g /= sum[1];
        k->data[idx].b /= sum[2];
    }

    printf("  Generated %dx%d Airy kernel.\n", k->width, k->height);
    return k;
}


// --- Free 2D Kernel ---
void free_kernel2d(Kernel2D *k)
{
    if (k)
    {
        free(k->data);
        free(k);
    }
}


// --- Symmetric Boundary Handling Helper ---
// Scipy 'symm': reflect about the *center* of the edge pixel.
// x_reflected = -x - 1 for x < 0
// x_reflected = 2*W - x - 1 for x >= W
static void get_symmetric_coords(int W, int H, int x, int y, int *sx, int *sy)
{
    // Handle X coordinate
    if (x < 0) { *sx = -x - 1; }
    else if (x >= W) { *sx = 2 * W - x - 1; }
    else { *sx = x; }
    // Clamp just in case reflection logic goes wrong (shouldn't happen with correct logic)
    *sx = fmax(0, fmin(W - 1, *sx));

    // Handle Y coordinate
    if (y < 0) { *sy = -y - 1; }
    else if (y >= H) { *sy = 2 * H - y - 1; }
    else { *sy = y; }
    *sy = fmax(0, fmin(H - 1, *sy));
}


// --- 2D Convolution (Direct Method) ---
bool convolve2d_rgb(const ImageF *src, ImageF *dst, const Kernel2D *k)
{
    if (!src || !dst || !k || !src->pixels || !dst->pixels || !k->data)
    {
        fprintf(stderr, "Error: NULL pointer passed to convolve2d_rgb.\n");
        return false;
    }
    if (src->width != dst->width || src->height != dst->height)
    {
        fprintf(stderr, "Error: Source and destination image dimensions must match for convolution.\n");
        return false;
    }

    int W = src->width;
    int H = src->height;
    int k_size = k->size; // Kernel radius
    int k_width = k->width;

    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {

            ColorRGB accumulator = {0.0, 0.0, 0.0};

            // Apply the kernel centered at (x, y)
            for (int ky = -k_size; ky <= k_size; ++ky)
            {
                for (int kx = -k_size; kx <= k_size; ++kx)
                {
                    // Calculate the corresponding source image coordinates
                    int src_x_raw = x - kx; // Kernel is flipped for convolution vs correlation
                    int src_y_raw = y - ky;

                    // Apply symmetric boundary conditions
                    int src_x, src_y;
                    get_symmetric_coords(W, H, src_x_raw, src_y_raw, &src_x, &src_y);

                    // Get kernel value (kernel coords are relative to center)
                    int kernel_idx = (ky + k_size) * k_width + (kx + k_size);
                    ColorRGB kernel_val = k->data[kernel_idx];

                    if (kernel_val.r < EPSILON_LOOSE && kernel_val.g < EPSILON_LOOSE && kernel_val.b < EPSILON_LOOSE) { continue; }

                    // Get source pixel value
                    int src_idx = src_y * W + src_x;
                    ColorRGB src_val = src->pixels[src_idx];

                    // Accumulate: C = C + S * K
                    accumulator.r += src_val.r * kernel_val.r;
                    accumulator.g += src_val.g * kernel_val.g;
                    accumulator.b += src_val.b * kernel_val.b;
                }
            }

            // Store the result in the destination image
            int dst_idx = y * W + x;
            dst->pixels[dst_idx] = accumulator;
        }

        if (y % 10 == 0)
        {
            printf("\r  Convolution progress: %d / %d rows", y, H);
            fflush(stdout);
        }
    }

    printf("\n  Convolution finished.\n");
    return true;
}

// --- Generate 1D Gaussian Kernel ---
Kernel1D *generate_gaussian_kernel_1d(double sigma, int size)
{
    if (sigma <= 0)
    {
        fprintf(stderr, "Warning: Gaussian sigma must be positive. Using default sigma=1.0.\n");
        sigma = 1.0;
    }

    // If size is not specified, determine from sigma (e.g., cover +/- 3 sigma)
    if (size <= 0)
    {
        size = (int)ceil(3.0 * sigma);
        if (size == 0) size = 1; // Ensure at least 3 elements
    }

    Kernel1D *k = (Kernel1D *)malloc(sizeof(Kernel1D));
    if (!k)
    {
        fprintf(stderr, "Error: Failed to allocate memory for Kernel1D struct.\n");
        return NULL;
    }

    k->size = size;
    k->length = 2 * size + 1;
    k->data = (double *)malloc(k->length * sizeof(double));
    if (!k->data)
    {
        fprintf(stderr, "Error: Failed to allocate memory for 1D kernel data (%d elements).\n", k->length);
        free(k);
        return NULL;
    }

    double sum = 0.0;
    double sigma_sq = sigma * sigma;
    // double scale = 1.0 / (sqrt(2.0 * M_PI) * sigma); // Normalization factor for continuous Gaussian

    for (int i = -size; i <= size; ++i)
    {
        const double x = i;
        // Calculate the discrete Gaussian value at x
        double discrete_val = exp(-(x * x) / (2.0 * sigma_sq));
        k->data[i + size] = discrete_val;
        sum += discrete_val;
    }

    // Normalize the discrete kernel sum to 1.0
    if (sum > 1e-9)
    {
        for (int i = 0; i < k->length; ++i) { k->data[i] /= sum; }
    }
    else
    {
        fprintf(stderr, "Warning: 1D Gaussian kernel sum is close to zero. Kernel will be invalid.\n");
        // Maybe set center element to 1.0?
        if (k->length > 0)
        {
            memset(k->data, 0, k->length * sizeof(double));
            k->data[k->size] = 1.0; // Center element (identity kernel)
        }
    }

    printf("Generated %d-element 1D Gaussian kernel (sigma=%.2f, size=%d).\n", k->length, sigma, size);
    return k;
}

// --- Free 1D Kernel ---
void free_kernel1d(Kernel1D *k)
{
    if (k)
    {
        free(k->data);
        free(k);
    }
}

// --- 1D Horizontal Convolution ---
bool convolve1d_h_rgb(const ImageF *src, ImageF *dst, const Kernel1D *k)
{
    if (!src || !dst || !k || !src->pixels || !dst->pixels || !k->data)
    {
        fprintf(stderr, "Error: NULL pointer passed to convolve1d_h_rgb.\n");
        return false;
    }
    if (src->width != dst->width || src->height != dst->height)
    {
        fprintf(stderr, "Error: Source and destination image dimensions must match for convolution.\n");
        return false;
    }

    int W = src->width;
    int H = src->height;
    int k_size = k->size;

    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            ColorRGB accumulator = {0.0, 0.0, 0.0};
            for (int kx = -k_size; kx <= k_size; ++kx)
            {
                int src_x_raw = x - kx; // Convolution flip

                // Apply boundary conditions (only need X coord here)
                int src_x, dummy_y;                                         // Don't need y coord from helper
                get_symmetric_coords(W, H, src_x_raw, y, &src_x, &dummy_y); // Pass current y

                double kernel_val = k->data[kx + k_size];
                int src_idx = y * W + src_x; // Use reflected x, original y
                ColorRGB src_val = src->pixels[src_idx];

                // Accumulate (kernel is scalar, applied to all channels)
                accumulator.r += src_val.r * kernel_val;
                accumulator.g += src_val.g * kernel_val;
                accumulator.b += src_val.b * kernel_val;
            }
            int dst_idx = y * W + x;
            dst->pixels[dst_idx] = accumulator;
        }
    }
    printf("Horizontal 1D convolution finished.\n");
    return true;
}


// --- 1D Vertical Convolution ---
bool convolve1d_v_rgb(const ImageF *src, ImageF *dst, const Kernel1D *k)
{
    if (!src || !dst || !k || !src->pixels || !dst->pixels || !k->data)
    {
        fprintf(stderr, "Error: NULL pointer passed to convolve1d_v_rgb.\n");
        return false;
    }
    if (src->width != dst->width || src->height != dst->height)
    {
        fprintf(stderr, "Error: Source and destination image dimensions must match for convolution.\n");
        return false;
    }

    int W = src->width;
    int H = src->height;
    int k_size = k->size;

    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            ColorRGB accumulator = {0.0, 0.0, 0.0};
            for (int ky = -k_size; ky <= k_size; ++ky)
            {
                int src_y_raw = y - ky; // Convolution flip

                // Apply boundary conditions (only need Y coord here)
                int src_y, dummy_x;                                         // Don't need x coord from helper
                get_symmetric_coords(W, H, x, src_y_raw, &dummy_x, &src_y); // Pass current x

                double kernel_val = k->data[ky + k_size];
                int src_idx = src_y * W + x; // Use reflected y, original x
                ColorRGB src_val = src->pixels[src_idx];

                // Accumulate
                accumulator.r += src_val.r * kernel_val;
                accumulator.g += src_val.g * kernel_val;
                accumulator.b += src_val.b * kernel_val;
            }
            int dst_idx = y * W + x;
            dst->pixels[dst_idx] = accumulator;
        }
    }
    printf("Vertical 1D convolution finished.\n");
    return true;
}
