#ifndef COLOR_H
#define COLOR_H

#include <stdint.h>

typedef struct { double r, g, b; } ColorRGB;

extern const ColorRGB COLOR_BLACK;
extern const ColorRGB COLOR_WHITE;

// Basic Operations
ColorRGB color_add(ColorRGB a, ColorRGB b);
ColorRGB color_mul_scalar(ColorRGB c, double s);
ColorRGB color_mul(ColorRGB a, ColorRGB b); // Component-wise
ColorRGB color_clamp(ColorRGB c, double min_val, double max_val);

// Blending
ColorRGB blend_colors(ColorRGB bg_col, double bg_alpha, ColorRGB fg_col, double fg_alpha);
ColorRGB blend_colors_over(ColorRGB bg_col, double bg_alpha, ColorRGB fg_col, double fg_alpha);
double blend_alpha(double bg_alpha, double fg_alpha);

#ifdef USE_ORIGINAL_BLENDING
    #define BLEND_COLORS(cb, alpha_b, ca, alpha_a) blend_colors(cb, alpha_b, ca, alpha_a)
#else
    #define BLEND_COLORS(cb, alpha_b, ca, alpha_a) blend_colors_over(cb, alpha_b, ca, alpha_a)
#endif

// sRGB Conversion
double linear_to_srgb(double v);
double srgb_to_linear(double v);
ColorRGB color_linear_to_srgb(ColorRGB c);
ColorRGB color_srgb_to_linear(ColorRGB c);

// Utility to convert to output format
typedef struct { uint8_t r, g, b; } ColorRGB_u8;

ColorRGB_u8 color_to_u8(ColorRGB c);

// Tonemapping
ColorRGB aces_fitted(ColorRGB in);

#endif // COLOR_H
