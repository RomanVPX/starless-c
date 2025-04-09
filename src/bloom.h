#ifndef BLOOM_H
#define BLOOM_H

#include "image.h" // For ImageF structure
#include "color.h" // For ColorRGB
#include <stdbool.h>

// Structure to hold a convolution kernel
typedef struct {
    int size;        // Kernel is (2*size+1) x (2*size+1)
    int width;       // width = 2*size+1
    int height;      // height = 2*size+1
    ColorRGB *data;  // Kernel data (RGB), size width*height
} Kernel;

// Function to generate the Airy disk kernel
// scale: 3-element array for RGB scaling factors
// size: Determines kernel dimensions (2*size+1)
Kernel* generate_airy_kernel(const double scale[3], int size);

// Function to free the kernel memory
void free_kernel(Kernel* k);

// Function to perform 2D convolution on an ImageF
// Applies kernel k to input image src, stores result in dst.
// Assumes src and dst are already allocated and have the same dimensions.
// Uses symmetric boundary handling ('symm' from scipy).
bool convolve2d_rgb(const ImageF *src, ImageF *dst, const Kernel *k);

// --- Bessel J1 function ---
// We need this for the Airy disk. C99/POSIX should provide it in math.h
// If not, we'd need to implement or find an alternative.
#include <math.h>
// double j1(double x); // Should be declared in math.h

#endif // BLOOM_H
