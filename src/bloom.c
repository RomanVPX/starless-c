#if defined(_MSC_VER)
    #define _USE_MATH_DEFINES
#endif
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // For memset
#include <time.h>
#include "bloom.h"
#include "core_constants.h"

#define AIRY_VIA_FFT

#ifdef AIRY_VIA_FFT
    #define MEOW_FFT_IMPLEMENTATION
    #include "meow_fft.h"

    static int next_pow2(int n)
    {
        int p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    static void transpose_complex(Meow_FFT_Complex* src, Meow_FFT_Complex* dst, int w, int h) 
    {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                dst[x * h + y] = src[y * w + x];
            }
        }
    }
#endif

#define AIRY_CONVOLUTION_TILE_SIZE 64

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

    clock_t start_time = clock();

    for (int j = -size; j <= size; ++j)
    {
        for (int i = -size; i <= size; ++i)
        {
            double r = sqrt((double)(i * i + j * j)) + EPSILON_STRICT;
            int kernel_idx = (j + size) * k->width + (i + size);

            double val_r = airy_disk_func(r / scale[0]);
            double val_g = airy_disk_func(r / scale[1]);
            double val_b = airy_disk_func(r / scale[2]);

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

    clock_t end_time = clock();
    double elapsed_time_ms = (double)(end_time - start_time) / CLOCKS_PER_SEC * 1000.0;
    printf("  Generated %dx%d Airy kernel.\n", k->width, k->height);
    printf("  Airy kernel generation took %.3f ms.\n", elapsed_time_ms);
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

    clock_t start_time = clock();

    for (int tile_y = 0; tile_y < H; tile_y += AIRY_CONVOLUTION_TILE_SIZE)
    {
        for (int tile_x = 0; tile_x < W; tile_x += AIRY_CONVOLUTION_TILE_SIZE)
        {
            int max_y = fmin(H, tile_y + AIRY_CONVOLUTION_TILE_SIZE);
            int max_x = fmin(W, tile_x + AIRY_CONVOLUTION_TILE_SIZE);

            for (int y = tile_y; y < max_y; ++y)
            {
                for (int x = tile_x; x < max_x; ++x)
                {
                    ColorRGB accumulator = {0.0, 0.0, 0.0};

                    for (int ky = -k_size; ky <= k_size; ++ky)
                    {
                        for (int kx = -k_size; kx <= k_size; ++kx)
                        {
                            ColorRGB kernel_val = k->data[(ky + k_size) * k_width + (kx + k_size)];

                            int src_x_raw = x - kx;
                            int src_y_raw = y - ky;

                            int src_x, src_y;
                            get_symmetric_coords(W, H, src_x_raw, src_y_raw, &src_x, &src_y);

                            int src_idx = src_y * W + src_x;
                            ColorRGB src_val = src->pixels[src_idx];

                            accumulator.r += src_val.r * kernel_val.r;
                            accumulator.g += src_val.g * kernel_val.g;
                            accumulator.b += src_val.b * kernel_val.b;
                        }
                    }
                    dst->pixels[y * W + x] = accumulator;
                }
            }
        }

        if (tile_y % AIRY_CONVOLUTION_TILE_SIZE == 0)
        {
            printf("\r  Tiled convolution progress: %d / %d rows", tile_y, H);
            fflush(stdout);
        }
    }

    clock_t end_time = clock();
    double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;

    printf("\n  Convolution finished in %.4f seconds.\n", elapsed_time);
    return true;
}


// --- Generate 1D Gaussian Kernel ---
Kernel1D *generate_gaussian_kernel_1d(double sigma, int size)
{
    if (sigma <= 0)
    {
        fprintf(stderr, "    Warning: Gaussian sigma must be positive. Using default sigma=1.0.\n");
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
        fprintf(stderr, "!   Error: Failed to allocate memory for Kernel1D struct.\n");
        return NULL;
    }

    k->size = size;
    k->length = 2 * size + 1;
    k->data = (double *)malloc(k->length * sizeof(double));
    if (!k->data)
    {
        fprintf(stderr, "!   Error: Failed to allocate memory for 1D kernel data (%d elements).\n", k->length);
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
    if (sum > EPSILON_STRICT)
    {
        for (int i = 0; i < k->length; ++i) { k->data[i] /= sum; }
    }
    else
    {
        fprintf(stderr, "    Warning: 1D Gaussian kernel sum is close to zero. Kernel will be invalid.\n");
        if (k->length > 0)
        {
            memset(k->data, 0, k->length * sizeof(double));
            k->data[k->size] = 1.0; // Center element (identity kernel)
        }
    }

    printf("    Generated %d-element 1D Gaussian kernel (sigma=%.2f, size=%d).\n", k->length, sigma, size);
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
        fprintf(stderr, "!   Error: NULL pointer passed to convolve1d_h_rgb.\n");
        return false;
    }
    if (src->width != dst->width || src->height != dst->height)
    {
        fprintf(stderr, "!   Error: Source and destination image dimensions must match for convolution.\n");
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
    printf("    Horizontal 1D convolution finished.\n");
    return true;
}


// --- 1D Vertical Convolution ---
bool convolve1d_v_rgb(const ImageF *src, ImageF *dst, const Kernel1D *k)
{
    if (!src || !dst || !k || !src->pixels || !dst->pixels || !k->data)
    {
        fprintf(stderr, "!   Error: NULL pointer passed to convolve1d_v_rgb.\n");
        return false;
    }
    if (src->width != dst->width || src->height != dst->height)
    {
        fprintf(stderr, "!   Error: Source and destination image dimensions must match for convolution.\n");
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
    printf("    Vertical 1D convolution finished.\n");
    return true;
}

// --- High-level Airy Bloom function ---
bool apply_airy_bloom(const ImageF *src, ImageF *dst, const double scale[3])
{
    if (!src || !dst || !src->pixels || !dst->pixels) return false;
    if (src->width != dst->width || src->height != dst->height) return false;

#ifdef AIRY_VIA_FFT
    int W = src->width;
    int H = src->height;
    
    int fft_w = next_pow2(W);
    int fft_h = next_pow2(H);
    
    printf("    Using FFT Airy Bloom. Image: %dx%d, FFT: %dx%d\n", W, H, fft_w, fft_h);
    clock_t start_time = clock();

    size_t row_ws_size = meow_fft_generate_workset(fft_w, NULL);
    size_t col_ws_size = meow_fft_generate_workset(fft_h, NULL);
    
    Meow_FFT_Workset* row_workset = (Meow_FFT_Workset*)malloc(row_ws_size);
    Meow_FFT_Workset* col_workset = (Meow_FFT_Workset*)malloc(col_ws_size);
    
    if (!row_workset || !col_workset)
    {
        fprintf(stderr, "!   Error: FFT Workset allocation failed.\n");
        free(row_workset); free(col_workset);
        return false;
    }
    
    meow_fft_generate_workset(fft_w, row_workset);
    meow_fft_generate_workset(fft_h, col_workset);

    size_t complex_count = (size_t)fft_w * fft_h;
    Meow_FFT_Complex* buf1 = (Meow_FFT_Complex*)malloc(complex_count * sizeof(Meow_FFT_Complex));
    Meow_FFT_Complex* buf2 = (Meow_FFT_Complex*)malloc(complex_count * sizeof(Meow_FFT_Complex));
    
    if (!buf1 || !buf2)
    {
        fprintf(stderr, "!   Error: FFT Buffer allocation failed.\n");
        free(row_workset); free(col_workset); free(buf1); free(buf2);
        return false;
    }

    for (int c = 0; c < 3; ++c)
    {
        memset(buf1, 0, complex_count * sizeof(Meow_FFT_Complex));
        
        // Copy Input (Pad with zeros)
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                double val = (c == 0) ? src->pixels[y*W + x].r : (c == 1) ? src->pixels[y*W + x].g : src->pixels[y*W + x].b;
                buf1[y * fft_w + x].r = (float)val;
                buf1[y * fft_w + x].j = 0.0f;
            }
        }
        
        // Forward FFT (Rows) -> buf2
        for (int y = 0; y < fft_h; ++y)
        {
            meow_fft(row_workset, &buf1[y * fft_w], &buf2[y * fft_w]);
        }
        
        // Transpose (buf2 -> buf1). Result is fft_h x fft_w
        transpose_complex(buf2, buf1, fft_w, fft_h); 
        
        // Forward FFT (Cols - now rows of transposed) -> buf2
        for (int x = 0; x < fft_w; ++x)
        {
            meow_fft(col_workset, &buf1[x * fft_h], &buf2[x * fft_h]);
        }
        
        // buf2 is now in Frequency Domain (Transposed).
        // buf2[x * fft_h + y] corresponds to freq (x, y)
        // x corresponds to original width freq (u), y to original height freq (v)
        
        // Precompute constants for OTF
        double s = scale[c];
        double cutoff_const = M_PI * s;
        
        for (int x = 0; x < fft_w; ++x)
        {
            int u = (x <= fft_w/2) ? x : x - fft_w;
            double u_sq = (double)u * u / ((double)fft_w * fft_w);
            
            for (int y = 0; y < fft_h; ++y)
            {
                int v = (y <= fft_h/2) ? y : y - fft_h;
                double v_sq = (double)v * v / ((double)fft_h * fft_h);
                
                double f = sqrt(u_sq + v_sq);
                double nu = f * cutoff_const;
                
                float otf = 0.0f;
                if (nu < 1.0) {
                     // 2/pi * (acos(nu) - nu * sqrt(1 - nu^2))
                     otf = (float)(2.0 / M_PI * (acos(nu) - nu * sqrt(1.0 - nu * nu)));
                }
                
                size_t idx = x * fft_h + y;
                // Complex multiplication with real OTF
                buf2[idx].r *= otf;
                buf2[idx].j *= otf;
            }
        }
        
        // Inverse FFT (Cols - Transposed) -> buf1
        for (int x = 0; x < fft_w; ++x)
        {
            meow_fft_i(col_workset, &buf2[x * fft_h], &buf1[x * fft_h]);
        }
        
        // Transpose back (buf1 -> buf2). Result is fft_w x fft_h
        transpose_complex(buf1, buf2, fft_h, fft_w); 
        
        // Inverse FFT (Rows) -> buf1
        for (int y = 0; y < fft_h; ++y)
        {
            meow_fft_i(row_workset, &buf2[y * fft_w], &buf1[y * fft_w]);
        }
        
        // Copy back to dst (Normalize and Crop)
        double norm = 1.0 / (double)(complex_count);
        
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                double val = buf1[y * fft_w + x].r * norm;
                if (c == 0) dst->pixels[y*W + x].r = val;
                if (c == 1) dst->pixels[y*W + x].g = val;
                if (c == 2) dst->pixels[y*W + x].b = val;
            }
        }
    }
    
    free(row_workset);
    free(col_workset);
    free(buf1);
    free(buf2);
    
    double elapsed = (double)(clock() - start_time) / CLOCKS_PER_SEC;
    printf("    FFT convolution finished in %.4f seconds.\n", elapsed);
    return true;

#else
    int W = src->width;
    int H = src->height;
    
    double max_intensity = 0.0;
    for (int i = 0; i < W * H; ++i)
    {
        max_intensity = fmax(max_intensity, fmax(src->pixels[i].r, fmax(src->pixels[i].g, src->pixels[i].b)));
    }
    
    int kernel_size_radius = (int)(25.0 * pow(fmax(0.1, max_intensity) / 5.0, 1.0 / 3.0) * (double)W / 1920.0);
    kernel_size_radius = fmax(1, kernel_size_radius);
    kernel_size_radius = fmin(100, kernel_size_radius);

    printf("    [Spatial] Max Intensity: %f, Kernel Radius: %d\n", max_intensity, kernel_size_radius);

    Kernel2D *airy_kernel = generate_airy_kernel(scale, kernel_size_radius);
    if (!airy_kernel) return false;
    
    printf("    Convolving image with Airy kernel (%dx%d)...\n", airy_kernel->width, airy_kernel->height);
    bool success = convolve2d_rgb(src, dst, airy_kernel);
    free_kernel2d(airy_kernel);
    return success;
#endif
}
