#ifndef BLOOM_H
#define BLOOM_H

#include "image.h" // For ImageF structure
#include "color.h" // For ColorRGB
#include <stdbool.h>

// --- 2D Kernel (for Airy) ---
typedef struct {
    int size;        // Kernel is (2*size+1) x (2*size+1)
    int width;       // width = 2*size+1
    int height;      // height = 2*size+1
    ColorRGB *data;  // Kernel data (RGB), size width*height
} Kernel2D;

void free_kernel2d(Kernel2D* k);
bool convolve2d_rgb(const ImageF *src, ImageF *dst, const Kernel2D *k);

// --- High-level Airy Bloom function ---
bool apply_airy_bloom(const ImageF *src, ImageF *dst, const double scale[3]);

// --- 1D Kernel (for Gaussian) ---
typedef struct {
    int size;       // Kernel has (2*size+1) elements
    int length;     // length = 2*size+1
    double *data;   // Kernel data (single array, applies to all RGB channels)
} Kernel1D;

// Function to generate a 1D Gaussian kernel
// sigma: Standard deviation of the Gaussian
// size: Determines kernel length (2*size+1). If <=0, calculated based on sigma.
Kernel1D* generate_gaussian_kernel_1d(double sigma, int size);

// Function to free the 1D kernel memory
void free_kernel1d(Kernel1D* k);

// Function to perform 1D horizontal convolution on an ImageF
// Applies kernel k horizontally to input image src, stores result in dst.
// Assumes src and dst are allocated and have the same dimensions.
// Uses symmetric boundary handling.
bool convolve1d_h_rgb(const ImageF *src, ImageF *dst, const Kernel1D *k, int num_threads);

// Function to perform 1D vertical convolution on an ImageF
// Applies kernel k vertically to input image src, stores result in dst.
// Assumes src and dst are allocated and have the same dimensions.
// Uses symmetric boundary handling.
bool convolve1d_v_rgb(const ImageF *src, ImageF *dst, const Kernel1D *k, int num_threads);


#endif // BLOOM_H
