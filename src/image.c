#include "image.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write_ext.h"

// For the finicky Linux compiler:
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

// Pre-computed lookup table: maps uint8 sRGB value directly to linear double.
// Covers all 256 possible input values, so no per-sample pow() is needed.
static double srgb_to_linear_lut[256];
static bool srgb_lut_initialized = false;

static void ensure_srgb_lut(void)
{
    if (srgb_lut_initialized) return;
    for (int i = 0; i < 256; ++i)
    {
        srgb_to_linear_lut[i] = srgb_to_linear((double)i / 255.0);
    }
    srgb_lut_initialized = true;
}

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
{
    if (!cfg || !metadata_output || !text_buffers_output || max_metadata_entries <= 0) { return 0; }

    int current_entry_index = 0;

    helper_add_meta_entry(metadata_output, text_buffers_output,
        &current_entry_index, max_metadata_entries, "Software", "Starless-C");
    helper_add_meta_entry(metadata_output, text_buffers_output,
        &current_entry_index, max_metadata_entries, "Software Repo", "https://github.com/RomanVPX/starless-c");
    helper_add_meta_entry(metadata_output, text_buffers_output,
        &current_entry_index, max_metadata_entries, "Software Version", "0.4.0");

    #define INIT_STRING(fieldName, pngKeySuffix) if(cfg->fieldName && pngKeySuffix[0] != '\0') \
        helper_add_meta_entry(metadata_output, text_buffers_output, \
        &current_entry_index, max_metadata_entries, pngKeySuffix, cfg->fieldName)

    #define INIT_INT(fieldName, pngKeySuffix) if (pngKeySuffix[0] != '\0') \
        helper_add_meta_int(metadata_output, text_buffers_output,\
        &current_entry_index, max_metadata_entries, pngKeySuffix, cfg->fieldName)

    #define INIT_DOUBLE(fieldName, pngKeySuffix) if (pngKeySuffix[0] != '\0') \
        helper_add_meta_double(metadata_output, text_buffers_output,\
        &current_entry_index, max_metadata_entries, pngKeySuffix, cfg->fieldName)

    #define INIT_BOOL(fieldName, pngKeySuffix) if (pngKeySuffix[0] != '\0') \
        helper_add_meta_bool(metadata_output, text_buffers_output,\
        &current_entry_index, max_metadata_entries, pngKeySuffix, cfg->fieldName)

    #define INIT_VEC3(fieldName, pngKeySuffix) if (pngKeySuffix[0] != '\0') \
        helper_add_meta_vec3d(metadata_output, text_buffers_output,\
        &current_entry_index, max_metadata_entries, pngKeySuffix, cfg->fieldName)

    #define INIT_INT_ARRAY2(fieldName, pngKeySuffix) if (pngKeySuffix[0] != '\0') \
        helper_add_meta_int_array2(metadata_output, text_buffers_output,\
        &current_entry_index, max_metadata_entries, pngKeySuffix, cfg->fieldName)

    #define INIT_SMART_ENUM(fieldName, pngKeySuffix) if (pngKeySuffix[0] != '\0') { \
        const char *enum_name = NULL; \
        if (strcmp(#fieldName, "disk_texture_mode") == 0) enum_name = disk_texture_mode_names[cfg->fieldName]; \
        else if (strcmp(#fieldName, "sky_texture_mode") == 0) enum_name = sky_texture_mode_names[cfg->fieldName]; \
        if (enum_name) helper_add_meta_entry(metadata_output, text_buffers_output, &current_entry_index, max_metadata_entries, pngKeySuffix, enum_name); \
    }

    #define INIT_NULL(fieldName, pngKeySuffix) /* Do not write those to metadata */

    #define FIELD_DEF(fieldName, cfgKey, initMacro, defLiteral) initMacro(fieldName, cfgKey)
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

void free_texture(Texture *tex)
{
    if (tex)
    {
        if (tex->data) { stbi_image_free(tex->data); }
        free(tex);
    }
}

// Fetch a single channel as linear-space double, with edge clamping.
static inline double fetch_linear_sample(const Texture *tex, int x, int y, int ch, bool srgb_in)
{
    if (x < 0) x = 0;
    else if (x >= tex->width) x = tex->width - 1;
    if (y < 0) y = 0;
    else if (y >= tex->height) y = tex->height - 1;

    unsigned char v = tex->data[(y * tex->width + x) * tex->channels + ch];
    return srgb_in ? srgb_to_linear_lut[v] : (double)v / 255.0;
}

// Catmull-Rom cubic weight (a = -0.5).
static inline double cubic_weight(double x)
{
    double ax = fabs(x);
    if (ax < 1.0) return (1.5 * ax - 2.5) * ax * ax + 1.0;
    if (ax < 2.0) return ((-0.5 * ax + 2.5) * ax - 4.0) * ax + 2.0;
    return 0.0;
}

static ColorRGB texture_lookup_nearest(const Texture *tex, double u, double v, bool srgb_in)
{
    u = fmax(0.0, fmin(0.99999, u));
    v = fmax(0.0, fmin(0.99999, v));

    int x = (int)(u * tex->width);
    int y = (int)(v * tex->height);

    int index = (y * tex->width + x) * tex->channels;

    if (srgb_in)
    {
        return (ColorRGB){
            srgb_to_linear_lut[tex->data[index + 0]],
            srgb_to_linear_lut[tex->data[index + 1]],
            srgb_to_linear_lut[tex->data[index + 2]]
        };
    }
    return (ColorRGB){
        tex->data[index + 0] / 255.0,
        tex->data[index + 1] / 255.0,
        tex->data[index + 2] / 255.0
    };
}

static ColorRGB texture_lookup_bicubic(const Texture *tex, double u, double v, bool srgb_in)
{
    double fx = u * tex->width - 0.5;
    double fy = v * tex->height - 0.5;

    int ix = (int)floor(fx);
    int iy = (int)floor(fy);
    double tx = fx - ix;
    double ty = fy - iy;

    double wx[4] = {
        cubic_weight(1.0 + tx),
        cubic_weight(tx),
        cubic_weight(1.0 - tx),
        cubic_weight(2.0 - tx)
    };
    double wy[4] = {
        cubic_weight(1.0 + ty),
        cubic_weight(ty),
        cubic_weight(1.0 - ty),
        cubic_weight(2.0 - ty)
    };

    ColorRGB out = {0.0, 0.0, 0.0};
    for (int j = 0; j < 4; ++j)
    {
        int sy = iy - 1 + j;
        double wyj = wy[j];
        for (int i = 0; i < 4; ++i)
        {
            int sx = ix - 1 + i;
            double w = wx[i] * wyj;
            out.r += fetch_linear_sample(tex, sx, sy, 0, srgb_in) * w;
            out.g += fetch_linear_sample(tex, sx, sy, 1, srgb_in) * w;
            out.b += fetch_linear_sample(tex, sx, sy, 2, srgb_in) * w;
        }
    }

    // Catmull-Rom may produce negative lobes, so...
    if (out.r < 0.0) out.r = 0.0;
    if (out.g < 0.0) out.g = 0.0;
    if (out.b < 0.0) out.b = 0.0;
    return out;
}

ColorRGB texture_lookup(const Texture *tex, double u, double v, bool srgb_in, bool bicubic)
{
    if (!tex || !tex->data) { return COLOR_BLACK; }
    if (srgb_in) ensure_srgb_lut();

    return bicubic ? texture_lookup_bicubic(tex, u, v, srgb_in)
                   : texture_lookup_nearest(tex, u, v, srgb_in);
}

bool save_debug_mask_png(const unsigned char *mask, int width, int height, const char *filename)
{
    if (!mask || width <= 0 || height <= 0 || !filename) return false;
    int ok = stbi_write_png(filename, width, height, 1, mask, width);
    if (!ok) { fprintf(stderr, "!   Error writing mask PNG '%s'\n", filename); return false; }
    return true;
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
        printf("    Attempting to save image with %d metadata entries...\n", num_metadata_entries);
        success = stbi_write_png_with_metadata(filename, width, height, 3, output_data, width * 3, metadata_array, num_metadata_entries);
    }
    else
    {   // Using basic stbi_write_png in this case just to be on the safe side
        printf("    Warning: No metadata generated, saving image without custom metadata...\n");
        success = stbi_write_png(filename, width, height, 3, output_data, width * 3);
    }

    free(output_data);

    if (!success)
    {
        fprintf(stderr, "!   Error writing PNG file '%s'\n", filename);
        return false;
    }

    printf("    Saved image to '%s' (metadata entries: %d)\n", filename, num_metadata_entries);
    return true;
}
