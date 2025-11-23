#include "image.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write_ext.h"

// Include stb_image_resize with warnings disabled on Windows:
#ifdef _WIN32
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wsign-compare"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#endif
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"
#ifdef _WIN32
#ifdef __clang__
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif
#endif

// For the finicky Linux compiler:
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

#define MAX_CONFIG_METADATA_ENTRIES 150

// metadata_array - where we write PngMetadata.
// text_buffers - array of char buffers for storing formatted values.
// current_idx_ptr - pointer to the current index in metadata_array and text_buffers.
// max_entries - maximum size of arrays.
static void helper_add_meta_entry(PngMetadata metadata_array[], char text_buffers[][256], int *current_idx_ptr, int max_entries,
                                  const char *key, const char *value_str)
{
    if (!value_str || *current_idx_ptr >= max_entries) return;
    // Keys are assumed to be string literals, so simply assigning a pointer.
    // stbi_write_png_with_metadata expects const char*, so this is fine.
    metadata_array[*current_idx_ptr].keyword = key;
    // Copy the formatted value to our allocated buffer
    snprintf(text_buffers[*current_idx_ptr], 256, "%s", value_str);
    metadata_array[*current_idx_ptr].text = text_buffers[*current_idx_ptr];

    (*current_idx_ptr)++;
}

static void helper_add_meta_int(PngMetadata m[], char tb[][256], int *idx, int max, const char *k, int v)
{
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%d", v);
    helper_add_meta_entry(m, tb, idx, max, k, buffer);
}

static void helper_add_meta_double(PngMetadata m[], char tb[][256], int *idx, int max, const char *k, double v)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%.7g", v);
    helper_add_meta_entry(m, tb, idx, max, k, buffer);
}

static void helper_add_meta_bool(PngMetadata m[], char tb[][256], int *idx, int max, const char *k, bool v)
{
    helper_add_meta_entry(m, tb, idx, max, k, v ? "true" : "false");
}

static void helper_add_meta_vec3d(PngMetadata m[], char tb[][256], int *idx, int max, const char *k, Vec3d v)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "(%.3f, %.3f, %.3f)", v.x, v.y, v.z);
    helper_add_meta_entry(m, tb, idx, max, k, buffer);
}

static void helper_add_meta_int_array2(PngMetadata m[], char tb[][256], int *idx, int max, const char *k, const int arr[2])
{
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "[%d, %d]", arr[0], arr[1]);
    helper_add_meta_entry(m, tb, idx, max, k, buffer);
}

int assemble_png_metadata(const Config *cfg, PngMetadata metadata_output[], char text_buffers_output[][256], int max_metadata_entries)
{                             /* Formatted values ↓ go here */
    if (!cfg || !metadata_output || !text_buffers_output || max_metadata_entries <= 0) { return 0; }

    int current_entry_index = 0;

    #define INIT_MACRO(fieldName, cfgKey) FIELD_DEF(fieldName, cfgKey, INIT_##fieldName, #fieldName)

    #define INIT_STRING(fieldName, pngKeySuffix) if(cfg->fieldName) \
        helper_add_meta_entry(metadata_output, text_buffers_output, \
        &current_entry_index, max_metadata_entries, pngKeySuffix, cfg->fieldName)

    #define INIT_INT(fieldName, pngKeySuffix) \
        helper_add_meta_int(metadata_output, text_buffers_output,\
        &current_entry_index, max_metadata_entries, pngKeySuffix, cfg->fieldName)

    #define INIT_DOUBLE(fieldName, pngKeySuffix) \
        helper_add_meta_double(metadata_output, text_buffers_output,\
        &current_entry_index, max_metadata_entries, pngKeySuffix, cfg->fieldName)

    #define INIT_BOOL(fieldName, pngKeySuffix) \
        helper_add_meta_bool(metadata_output, text_buffers_output,\
        &current_entry_index, max_metadata_entries, pngKeySuffix, cfg->fieldName)

    #define INIT_VEC3(fieldName, pngKeySuffix) \
        helper_add_meta_vec3d(metadata_output, text_buffers_output,\
        &current_entry_index, max_metadata_entries, pngKeySuffix, cfg->fieldName)

    #define INIT_INT_ARRAY2(fieldName, pngKeySuffix) \
        helper_add_meta_int_array2(metadata_output, text_buffers_output,\
        &current_entry_index, max_metadata_entries, pngKeySuffix, cfg->fieldName)

    #define INIT_ENUM(fieldName, pngKeySuffix) if (pngKeySuffix[0] != '\0') { \
        const char *enum_name = NULL; \
        if (strcmp(#fieldName, "disk_texture_mode") == 0) enum_name = disk_texture_mode_names[cfg->fieldName]; \
        else if (strcmp(#fieldName, "sky_texture_mode") == 0) enum_name = sky_texture_mode_names[cfg->fieldName]; \
        if (enum_name) helper_add_meta_entry(metadata_output, text_buffers_output, &current_entry_index, max_metadata_entries, pngKeySuffix, enum_name); \
    }
    #define INIT_NULL(fieldName, pngKeySuffix) /* Do not write those to metadata */

    #define FIELD_DEF(fieldName, cfgKey, initMacro, defLiteral) initMacro(fieldName, cfgKey)

    helper_add_meta_entry(metadata_output, text_buffers_output,
        &current_entry_index, max_metadata_entries, "Software", "Starless-C");
    helper_add_meta_entry(metadata_output, text_buffers_output,
        &current_entry_index, max_metadata_entries, "Software Repo", "https://github.com/RomanVPX/starless-c");

    #define SEC_ALL
    #include "x_config_fields.h"
    #undef FIELD_DEF

    return current_entry_index;
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
    memset(img->pixels, 0, width * height * sizeof(ColorRGB)); // Initialize pixels to black
    return img;
}

void free_imagef(ImageF *img)
{
    if (!img) { return; }
    free(img->pixels);
    free(img);
}

Texture *load_texture(const char *filename)
{
    Texture *tex = (Texture *)malloc(sizeof(Texture));
    if (!tex)
    {
        fprintf(stderr, "! Error: Could not allocate memory for Texture struct.\n");
        return NULL;
    }
    // Force 3 components (RGB) for simplicity, even if alpha exists
    tex->data = stbi_load(filename, &tex->width, &tex->height, &tex->channels, 3);
    if (!tex->data)
    {
        fprintf(stderr, "! Error loading texture '%s': %s\n", filename, stbi_failure_reason());
        free(tex);
        return NULL;
    }
    tex->channels = 3; // Always set channels to 3 since we requested 3 components.
    printf("  Loaded texture '%s' (%dx%d, %d channels reported by STB, forced to 3)\n", filename, tex->width, tex->height, tex->channels);

    return tex;
}

Texture *resize_texture(const Texture *input_tex, float scale_factor)
{
    if (!input_tex || !input_tex->data || scale_factor <= 0)
    {
        fprintf(stderr, "!   Error: Invalid input to resize_texture.\n");
        return NULL;
    }
    if (scale_factor == 1.0f)
    {
        fprintf(stderr, "    Warning: resize_texture called with scale_factor=1.0. No resize needed.\n");
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
        fprintf(stderr, "!   Error: Calculated output dimensions for resize are invalid (%dx%d).\n", out_w, out_h);
        return NULL;
    }

    printf("    Resizing texture from %dx%d to %dx%d (scale: %.2f)...\n", in_w, in_h, out_w, out_h, scale_factor);

    // Allocate memory for the output texture data
    size_t output_size = (size_t)out_w * out_h * channels;
    unsigned char *output_data = (unsigned char *)malloc(output_size);
    if (!output_data)
    {
        fprintf(stderr, "!   Error: Failed to allocate memory for resized texture data (%zu bytes).\n", output_size);
        return NULL;
    }
    // Use default flags/filter for now (STBIR_FILTER_DEFAULT which is Mitchell-Netravali, good quality)
    int success = stbir_resize_uint8(input_tex->data, in_w, in_h, 0, // 0 stride means default (in_w * channels)
                                     output_data, out_w, out_h, 0,   // 0 stride means default (out_w * channels)
                                     channels);
    if (!success)
    {
        fprintf(stderr, "!   Error: stbir_resize_uint8 failed.\n");
        free(output_data);
        return NULL;
    }

    Texture *output_tex = (Texture *)malloc(sizeof(Texture)); // A new Texture struct for the resized data
    if (!output_tex)
    {
        fprintf(stderr, "!   Error: Failed to allocate memory for resized Texture struct.\n");
        free(output_data);
        return NULL;
    }

    output_tex->width = out_w;
    output_tex->height = out_h;
    output_tex->channels = channels;
    output_tex->data = output_data;

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
    int y = (int)(v * tex->height); // Python code uses (v * height), assuming top-left origin

    int index = (y * tex->width + x) * tex->channels; // Assumes 3 channels (RGB)

    // Ensure index is within bounds (should be due to clamping, but belt-and-suspenders)
    if (index < 0 || index + 2 >= tex->width * tex->height * tex->channels)
    {
        fprintf(stderr, "  Warning: Texture lookup out of bounds (%d, %d) -> index %d\n", x, y, index);
        return COLOR_BLACK;
    }

    ColorRGB color;
    color.r = tex->data[index + 0] / 255.0;
    color.g = tex->data[index + 1] / 255.0;
    color.b = tex->data[index + 2] / 255.0;

    if (srgb_in) { return color_srgb_to_linear(color); }
    return color;
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

    PngMetadata metadata_array[MAX_CONFIG_METADATA_ENTRIES];
    char text_value_buffers[MAX_CONFIG_METADATA_ENTRIES][256]; // Array that keeps strings with values
    int num_metadata_entries = 0;

    num_metadata_entries = assemble_png_metadata(cfg, metadata_array, text_value_buffers, MAX_CONFIG_METADATA_ENTRIES);

    int success = 0;
    if (num_metadata_entries > 0)
    {
        printf("  Attempting to save image with %d metadata entries...\n", num_metadata_entries);
        success = stbi_write_png_with_metadata(filename, width, height, 3, output_data, width * 3, metadata_array, num_metadata_entries);
    }
    else
    {   // Using basic stbi_write_png in this case just to be on the safe side
        printf("  Warning: No metadata generated, saving image without custom metadata...\n");
        success = stbi_write_png(filename, width, height, 3, output_data, width * 3);
    }

    free(output_data);

    if (!success)
    {
        fprintf(stderr, "Error writing PNG file '%s'\n", filename);
        return false;
    }

    printf("Saved image to '%s' (metadata entries: %d)\n", filename, num_metadata_entries);
    return true;
}
