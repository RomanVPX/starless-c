#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>
#include "color.h"

typedef struct {
    int width;
    int height;
    int channels; // Usually 3 (RGB) or 4 (RGBA)
    unsigned char *data; // Raw pixel data (uint8) loaded by stb_image
} Texture;

typedef struct {
    int width;
    int height;
    ColorRGB *pixels; // Pixel data stored as doubles for precision during calculations
} ImageF; // Floating point image buffer

// Image Creation/Destruction
ImageF* create_imagef(int width, int height);
void free_imagef(ImageF *img);

// Texture Loading/Destruction
Texture* load_texture(const char *filename);
void free_texture(Texture *tex);

// Texture Resizing
Texture* resize_texture(const Texture* input_tex, float scale_factor);

// Texture Lookup (using nearest neighbor for now, like Python code)
ColorRGB texture_lookup(const Texture *tex, double u, double v, bool srgb_in);

// Saving Image
bool save_image_png(const ImageF *img, const char *filename, bool convert_to_srgb);

#endif // IMAGE_H
