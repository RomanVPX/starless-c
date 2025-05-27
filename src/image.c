#include "image.h"
#include "config.h"
// Define STB implementation modes *before* including the headers in *one* C file
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write_ext.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stb_image_resize.h"
// For the finicky Linux compiler:
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif


static char *config_to_json(const Config *cfg)
{
    char *json = (char *)malloc(4096);
    if (!json) return NULL;

    int pos = 0;
    pos += snprintf(json + pos, 4096 - pos, "{\n");

    // Resolution
    pos += snprintf(json + pos, 4096 - pos, "\"resolution\": [%d, %d],\n", cfg->resolution[0], cfg->resolution[1]);

    // Ray tracing params
    pos += snprintf(json + pos, 4096 - pos, "\"n_iterations\": %d,\n", cfg->n_iterations);
    pos += snprintf(json + pos, 4096 - pos, "\"step_size\": %f,\n", cfg->step_size);
    pos += snprintf(json + pos, 4096 - pos, "\"method\": \"%s\",\n", cfg->method == METH_LEAPFROG ? "leapfrog" : "rk4");

    // Camera
    pos += snprintf(json + pos, 4096 - pos, "\"camera\": {\n");
    pos += snprintf(json + pos, 4096 - pos, "  \"position\": [%f, %f, %f],\n", cfg->camera_pos.x, cfg->camera_pos.y, cfg->camera_pos.z);
    pos += snprintf(json + pos, 4096 - pos, "  \"look_at\": [%f, %f, %f],\n", cfg->look_at.x, cfg->look_at.y, cfg->look_at.z);
    pos += snprintf(json + pos, 4096 - pos, "  \"up\": [%f, %f, %f],\n", cfg->up_vector.x, cfg->up_vector.y, cfg->up_vector.z);
    pos += snprintf(json + pos, 4096 - pos, "  \"fov\": %f\n", atan(cfg->tan_fov) * 2 * 180 / M_PI);
    pos += snprintf(json + pos, 4096 - pos, "  },\n");

    // Disk
    pos += snprintf(json + pos, 4096 - pos, "\"disk\": {\n");
    pos += snprintf(json + pos, 4096 - pos, "  \"inner_radius\": %f,\n", cfg->disk_inner_radius);
    pos += snprintf(json + pos, 4096 - pos, "  \"outer_radius\": %f,\n", cfg->disk_outer_radius);
    pos += snprintf(json + pos, 4096 - pos, "  \"texture_mode\": \"%s\",\n",
                    cfg->disk_texture_mode == DT_NONE        ? "none"
                    : cfg->disk_texture_mode == DT_TEXTURE   ? "texture"
                    : cfg->disk_texture_mode == DT_SOLID     ? "solid"
                    : cfg->disk_texture_mode == DT_GRID      ? "grid"
                    : cfg->disk_texture_mode == DT_BLACKBODY ? "blackbody"
                                                             : "unknown");
    if (cfg->disk_texture_path) pos += snprintf(json + pos, 4096 - pos, "  \"texture_path\": \"%s\",\n", cfg->disk_texture_path);
    pos += snprintf(json + pos, 4096 - pos, "  \"horizon_grid\": %s\n", cfg->horizon_grid ? "true" : "false");
    pos += snprintf(json + pos, 4096 - pos, "  },\n");

    // Effects
    pos += snprintf(json + pos, 4096 - pos, "\"effects\": {\n");
    pos += snprintf(json + pos, 4096 - pos, "  \"distortion\": %s,\n", cfg->distort ? "true" : "false");
    if (cfg->fog_do) pos += snprintf(json + pos, 4096 - pos, "  \"fog_multiplier\": %f,\n", cfg->fog_mult);
    if (cfg->blur_do) pos += snprintf(json + pos, 4096 - pos, "  \"blur\": true,\n");
    if (cfg->airy_bloom) pos += snprintf(json + pos, 4096 - pos, "  \"airy_bloom_radius\": %f,\n", cfg->airy_radius);
    pos += snprintf(json + pos, 4096 - pos, "  \"gain\": %f,\n", cfg->gain);
    pos += snprintf(json + pos, 4096 - pos, "  \"aces_exposure\": %f\n", cfg->aces_exposure);
    pos += snprintf(json + pos, 4096 - pos, "  }\n");

    pos += snprintf(json + pos, 4096 - pos, "}\n");

    return json;
}


ImageF *create_imagef(int width, int height)
{
    ImageF *img = (ImageF *)malloc(sizeof(ImageF));
    if (!img) return NULL;
    img->width = width;
    img->height = height;
    img->pixels = (ColorRGB *)malloc(width * height * sizeof(ColorRGB));
    if (!img->pixels)
    {
        free(img);
        return NULL;
    }
    // Initialize pixels to black
    memset(img->pixels, 0, width * height * sizeof(ColorRGB));
    return img;
}


void free_imagef(ImageF *img)
{
    if (img)
    {
        free(img->pixels);
        free(img);
    }
}


Texture *load_texture(const char *filename)
{
    Texture *tex = (Texture *)malloc(sizeof(Texture));
    if (!tex)
    {
        fprintf(stderr, "Error: Could not allocate memory for Texture struct.\n");
        return NULL;
    }
    // Force 3 components (RGB) for simplicity, even if alpha exists
    tex->data = stbi_load(filename, &tex->width, &tex->height, &tex->channels, 3);
    if (!tex->data)
    {
        fprintf(stderr, "Error loading texture '%s': %s\n", filename, stbi_failure_reason());
        free(tex);
        return NULL;
    }
    // Always set channels to 3 since we requested 3 components.
    tex->channels = 3;
    printf("Loaded texture '%s' (%dx%d, %d channels reported by STB, forced to 3)\n", filename, tex->width, tex->height, tex->channels);

    return tex;
}


Texture *resize_texture(const Texture *input_tex, float scale_factor)
{
    if (!input_tex || !input_tex->data || scale_factor <= 0)
    {
        fprintf(stderr, "Error: Invalid input to resize_texture.\n");
        return NULL;
    }
    if (scale_factor == 1.0f)
    {
        fprintf(stderr, "Warning: resize_texture called with scale_factor=1.0. No resize needed.\n");

        return NULL; // Or return a copy if the caller expects a new texture always?
    }

    int in_w = input_tex->width;
    int in_h = input_tex->height;
    int channels = input_tex->channels; // Should be 3 based on our load logic

    // Calculate new dimensions
    int out_w = (int)(in_w * scale_factor + 0.5f); // Add 0.5 for rounding
    int out_h = (int)(in_h * scale_factor + 0.5f);

    if (out_w <= 0 || out_h <= 0)
    {
        fprintf(stderr, "Error: Calculated output dimensions for resize are invalid (%dx%d).\n", out_w, out_h);
        return NULL;
    }

    printf("Resizing texture from %dx%d to %dx%d (scale: %.2f)...\n", in_w, in_h, out_w, out_h, scale_factor);

    // Allocate memory for the output texture data
    size_t output_size = (size_t)out_w * out_h * channels;
    unsigned char *output_data = (unsigned char *)malloc(output_size);
    if (!output_data)
    {
        fprintf(stderr, "Error: Failed to allocate memory for resized texture data (%zu bytes).\n", output_size);
        return NULL;
    }
    // Perform the resize operation
    // Use default flags/filter for now (STBIR_FILTER_DEFAULT which is Mitchell-Netravali, good quality)
    int success = stbir_resize_uint8(input_tex->data, in_w, in_h, 0, // 0 stride means default (in_w * channels)
                                     output_data, out_w, out_h, 0,   // 0 stride means default (out_w * channels)
                                     channels);
    if (!success)
    {
        fprintf(stderr, "Error: stbir_resize_uint8 failed.\n");
        free(output_data);
        return NULL;
    }

    // Create a new Texture struct for the resized data
    Texture *output_tex = (Texture *)malloc(sizeof(Texture));
    if (!output_tex)
    {
        fprintf(stderr, "Error: Failed to allocate memory for resized Texture struct.\n");
        free(output_data);
        return NULL;
    }

    output_tex->width = out_w;
    output_tex->height = out_h;
    output_tex->channels = channels;
    output_tex->data = output_data;

    printf("Texture resizing successful.\n");
    return output_tex;
}


void free_texture(Texture *tex)
{
    if (tex)
    {
        if (tex->data) { stbi_image_free(tex->data); }
        free(tex);
    }
}


// Simple nearest neighbor lookup
ColorRGB texture_lookup(const Texture *tex, double u, double v, bool srgb_in)
{
    if (!tex || !tex->data)
    {
        return COLOR_BLACK; // Or some error color
    }
    // Clamp UV coordinates
    u = fmax(0.0, fmin(0.99999, u));
    v = fmax(0.0, fmin(0.99999, v)); // Use 1.0 - v if texture origin is bottom-left

    int x = (int)(u * tex->width);
    int y = (int)(v * tex->height);                   // Python code uses (v * height), assuming top-left origin

    int index = (y * tex->width + x) * tex->channels; // Assumes 3 channels (RGB)

    // Ensure index is within bounds (should be due to clamping, but belt-and-suspenders)
    if (index < 0 || index + 2 >= tex->width * tex->height * tex->channels)
    {
        fprintf(stderr, "Warning: Texture lookup out of bounds (%d, %d) -> index %d\n", x, y, index);
        return COLOR_BLACK;
    }

    ColorRGB color;
    color.r = tex->data[index + 0] / 255.0;
    color.g = tex->data[index + 1] / 255.0;
    color.b = tex->data[index + 2] / 255.0;

    if (srgb_in) { return color_srgb_to_linear(color); }
    else { return color; }
}


bool save_image_png(const ImageF *img, const char *filename, bool convert_to_srgb, const Config *cfg)
{
    if (!img || !img->pixels) { return false; }

    int width = img->width;
    int height = img->height;
    unsigned char *output_data = (unsigned char *)malloc(width * height * 3); // 3 channels (RGB)
    if (!output_data) return false;

    for (int i = 0; i < width * height; ++i)
    {
        ColorRGB pixel_color = img->pixels[i];

        // Apply sRGB conversion if requested
        if (convert_to_srgb) { pixel_color = color_linear_to_srgb(pixel_color); }

        // Clamp and convert to u8
        ColorRGB_u8 pixel_u8 = color_to_u8(pixel_color);

        output_data[i * 3 + 0] = pixel_u8.r;
        output_data[i * 3 + 1] = pixel_u8.g;
        output_data[i * 3 + 2] = pixel_u8.b;
    }
    // Generate metadata from config
    char *metadata_json = config_to_json(cfg);
    if (!metadata_json)
    {
        free(output_data);
        return false;
    }
    // Setup metadata
    PngMetadata metadata[] = {{"Title", "Black Hole Raytracer Output"}, {"Software", "starless-c"}, {"Comment", metadata_json}};

    int success = stbi_write_png_with_metadata(filename, width, height, 3, output_data, width * 3, metadata,
                                               sizeof(metadata) / sizeof(metadata[0]));
    free(output_data);
    free(metadata_json);

    if (!success)
    {
        fprintf(stderr, "Error writing PNG file '%s'\n", filename);
        return false;
    }

    printf("Saved image with metadata to '%s'\n", filename);
    return true;
}
