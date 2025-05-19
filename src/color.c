#include "color.h"
#include <math.h>
#include <stdio.h>

const ColorRGB COLOR_BLACK = {0.0, 0.0, 0.0};
const ColorRGB COLOR_WHITE = {1.0, 1.0, 1.0};

ColorRGB color_add(ColorRGB a, ColorRGB b) { return (ColorRGB){a.r + b.r, a.g + b.g, a.b + b.b}; }
ColorRGB color_mul_scalar(ColorRGB c, double s) { return (ColorRGB){c.r * s, c.g * s, c.b * s}; }
ColorRGB color_mul(ColorRGB a, ColorRGB b) { return (ColorRGB){a.r * b.r, a.g * b.g, a.b * b.b}; }

ColorRGB color_clamp(ColorRGB c, double min_val, double max_val) {
    return (ColorRGB){
        fmax(min_val, fmin(max_val, c.r)),
        fmax(min_val, fmin(max_val, c.g)),
        fmax(min_val, fmin(max_val, c.b))
    };
}

// ================================================================
// Blends foreground (ca, aalpha) OVER background (cb, balpha)
// USING THE PYTHON SCRIPT'S FORMULA: ca + cb * (balpha*(1.-aalpha))
// !!! Note: This is NOT the standard 'over' operator! !!!
// Arguments match Python call order: cb=background, balpha=background_alpha, ca=foreground, aalpha=foreground_alpha
ColorRGB blend_colors(ColorRGB cb, double balpha, ColorRGB ca, double aalpha) {
    // Calculate background contribution term: cb * (balpha * (1.0 - aalpha))
    ColorRGB background_term = color_mul_scalar(cb, balpha * (1.0 - aalpha));
    // Add foreground color (ca) to the background term
    return color_add(ca, background_term);
}
// ================================================================


// Note: Arguments here follow standard bg/fg naming, but match Python calculation
double blend_alpha(double bg_alpha, double fg_alpha) {
    // Python: blendalpha(balpha, aalpha) -> aalpha + balpha*(1.-aalpha)
    // Here: fg_alpha corresponds to aalpha, bg_alpha corresponds to balpha
    return fg_alpha + bg_alpha * (1.0 - fg_alpha);
}


double linear_to_srgb(double x) {
    if (x <= 0.0)
        return 0.0;
    else if (x >= 1.0)
        return 1.0;
    else if (x <= 0.0031308f)
        return x * 12.92;
    else
        return 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

double srgb_to_linear(double x) {
    if (x <= 0.0)
        return 0.0;
    else if (x >= 1.0)
        return 1.0;
    else if (x < 0.04045)
        return x / 12.92;
    else
        return pow((x + 0.055) / 1.055, 2.4);
}

ColorRGB color_linear_to_srgb(ColorRGB c) {
    return (ColorRGB){
        linear_to_srgb(c.r),
        linear_to_srgb(c.g),
        linear_to_srgb(c.b)
    };
}

ColorRGB color_srgb_to_linear(ColorRGB c) {
    return (ColorRGB){
        srgb_to_linear(c.r),
        srgb_to_linear(c.g),
        srgb_to_linear(c.b)
    };
}


ColorRGB_u8 color_to_u8(ColorRGB c) {
    // Assumes input color c is in the range [0, 1]
    return (ColorRGB_u8){
        (uint8_t)(fmax(0.0, fmin(1.0, c.r)) * 255.999),
        (uint8_t)(fmax(0.0, fmin(1.0, c.g)) * 255.999),
        (uint8_t)(fmax(0.0, fmin(1.0, c.b)) * 255.999)
    };
}
