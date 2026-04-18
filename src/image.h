#ifndef IMAGE_H
#define IMAGE_H

#include "color.h"
#include "config.h"


// Typedef-ed in config.h to avoid circular dependency
struct Texture {
    int width;
    int height;
    int channels;
    unsigned char *data;
};

// Typedef-ed in config.h to avoid circular dependency
struct ImageF {
    int width;
    int height;
    ColorRGB *pixels;
};

// Creates a new floating-point image buffer with the specified width and height.
// The pixel values are initialized to black (0.0, 0.0, 0.0).
ImageF *create_imagef(int width, int height);
void free_imagef(ImageF *img);

// Loads a texture from the specified file.
// Supported formats: PNG, JPEG, BMP, and others supported by the stb_image library.
// Parameters:
// - filename: The path to the image file to load.
// Returns:
// - A pointer to the loaded Texture, or NULL if loading fails.
Texture* load_texture(const char *filename);
void free_texture(Texture *tex);

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
// - bicubic: If true, uses bicubic (Catmull-Rom) filtering; otherwise nearest-neighbor.
ColorRGB texture_lookup(const Texture *tex, double u, double v, bool srgb_in, bool bicubic);

#endif // IMAGE_H
