#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>
#include "color.h"
#include "config.h"


typedef struct Texture {
    int width;
    int height;
    int channels; // Usually 3 (RGB) or 4 (RGBA)
    unsigned char *data; // Raw pixel data (uint8) loaded by the stb_image library, commonly used for image loading
} Texture;

typedef struct ImageF {
    int width;
    int height;
    ColorRGB *pixels; // Pixel data stored as doubles for high-precision color manipulation during image processing operations
} ImageF; // Floating point image buffer

// Creates a new floating-point image buffer with the specified width and height.
// The pixel values are initialized to black (0.0, 0.0, 0.0).
ImageF* create_imagef(int width, int height);
void free_imagef(ImageF *img);

// Loads a texture from the specified file.
// Supported formats: PNG, JPEG, BMP, and others supported by the stb_image library.
// Parameters:
// - filename: The path to the image file to load.
// Returns:
// - A pointer to the loaded Texture, or NULL if loading fails.
Texture* load_texture(const char *filename);
void free_texture(Texture *tex);

// Texture Resizing
Texture* resize_texture(const Texture* input_tex, float scale_factor);

// Parameters:
// - img: The floating-point image buffer to save.
// - filename: The name of the file to save the image to.
// - convert_to_srgb: If true, the image's colors will be converted from linear space to sRGB space.
bool save_image_png(const ImageF *img, const char *filename, bool convert_to_srgb, const Config *cfg);
// Parameters:
// - tex: Pointer to the texture to sample from.
// - u: Horizontal coordinate in normalized texture space [0.0, 1.0].
// - v: Vertical coordinate in normalized texture space [0.0, 1.0].
// - srgb_in: If true, assumes the texture is in sRGB color space and converts to linear space.
ColorRGB texture_lookup(const Texture *tex, double u, double v, bool srgb_in);

#endif // IMAGE_H
