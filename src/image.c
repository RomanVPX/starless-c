#include "image.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h> // for memset

// Define STB implementation modes *before* including the headers in *one* C file
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

ImageF* create_imagef(int width, int height) {
    ImageF *img = (ImageF*)malloc(sizeof(ImageF));
    if (!img) return NULL;
    img->width = width;
    img->height = height;
    img->pixels = (ColorRGB*)malloc(width * height * sizeof(ColorRGB));
    if (!img->pixels) {
        free(img);
        return NULL;
    }
    // Initialize pixels to black
    memset(img->pixels, 0, width * height * sizeof(ColorRGB));
    return img;
}

void free_imagef(ImageF *img) {
    if (img) {
        free(img->pixels);
        free(img);
    }
}

Texture* load_texture(const char *filename) {
    Texture *tex = (Texture*)malloc(sizeof(Texture));
    if (!tex) {
        fprintf(stderr, "Error: Could not allocate memory for Texture struct.\n");
        return NULL;
    }

    // Force 3 components (RGB) for simplicity, even if alpha exists
    tex->data = stbi_load(filename, &tex->width, &tex->height, &tex->channels, 3);

    if (!tex->data) {
        fprintf(stderr, "Error loading texture '%s': %s\n", filename, stbi_failure_reason());
        free(tex);
        return NULL;
    }
    // Always set channels to 3 since we requested 3 components.
    tex->channels = 3;

    printf("Loaded texture '%s' (%dx%d, %d channels reported by STB, forced to 3)\n",
           filename, tex->width, tex->height, tex->channels);

    return tex;
}

void free_texture(Texture *tex) {
    if (tex) {
        if (tex->data) {
            stbi_image_free(tex->data);
        }
        free(tex);
    }
}

// Simple nearest neighbor lookup
ColorRGB texture_lookup(const Texture *tex, double u, double v, bool srgb_in) {
    if (!tex || !tex->data) {
        return COLOR_BLACK; // Or some error color
    }

    // Clamp UV coordinates
    u = fmax(0.0, fmin(0.99999, u));
    v = fmax(0.0, fmin(0.99999, v)); // Use 1.0 - v if texture origin is bottom-left

    int x = (int)(u * tex->width);
    int y = (int)(v * tex->height); // Python code uses (v * height), assuming top-left origin

    int index = (y * tex->width + x) * tex->channels; // Assumes 3 channels (RGB)

    // Ensure index is within bounds (should be due to clamping, but belt-and-suspenders)
    if (index < 0 || index + 2 >= tex->width * tex->height * tex->channels) {
         fprintf(stderr, "Warning: Texture lookup out of bounds (%d, %d) -> index %d\n", x, y, index);
         return COLOR_BLACK;
    }

    ColorRGB color;
    color.r = tex->data[index + 0] / 255.0;
    color.g = tex->data[index + 1] / 255.0;
    color.b = tex->data[index + 2] / 255.0;

    if (srgb_in) {
        return color_srgb_to_linear(color);
    } else {
        return color;
    }
}

bool save_image_png(const ImageF *img, const char *filename, bool convert_to_srgb) {
    if (!img || !img->pixels) return false;

    int width = img->width;
    int height = img->height;
    unsigned char *output_data = (unsigned char*)malloc(width * height * 3); // 3 channels (RGB)
    if (!output_data) return false;

    for (int i = 0; i < width * height; ++i) {
        ColorRGB pixel_color = img->pixels[i];

        // Apply sRGB conversion if requested
        if (convert_to_srgb) {
            pixel_color = color_linear_to_srgb(pixel_color);
        }

        // Clamp and convert to u8
        ColorRGB_u8 pixel_u8 = color_to_u8(pixel_color);

        output_data[i * 3 + 0] = pixel_u8.r;
        output_data[i * 3 + 1] = pixel_u8.g;
        output_data[i * 3 + 2] = pixel_u8.b;
    }

    int success = stbi_write_png(filename, width, height, 3, output_data, width * 3);
    free(output_data);

    if (!success) {
        fprintf(stderr, "Error writing PNG file '%s'\n", filename);
        return false;
    }

    printf("Saved image to '%s'\n", filename);
    return true;
}
