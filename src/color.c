#include "color.h"
#include <math.h>
#include <stdio.h> // For potential debug output

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

// alpha blending (foreground over background)
ColorRGB blend_colors(ColorRGB background, double bg_alpha, ColorRGB foreground, double fg_alpha) {
    // Simple OVER operator: C_o = C_f * A_f + C_b * A_b * (1 - A_f)
    // Assuming input colors are *not* premultiplied by alpha
    // For non-premultiplied: C_o = C_f * A_f + C_b * (1 - A_f)  (This matches Python code logic)
    ColorRGB term1 = color_mul_scalar(foreground, fg_alpha);
    ColorRGB term2 = color_mul_scalar(background, (1.0 - fg_alpha) * bg_alpha); // bg_alpha only relevant if background itself is transparent
    // The Python version seems to assume background alpha is 1 implicitly for the blend step, let's replicate:
     ColorRGB term2_python_like = color_mul_scalar(background, 1.0 * (1.0 - fg_alpha));
    // Let's stick to the python logic for now: ca + cb * (balpha*(1.-aalpha))[:,np.newaxis]
    // Where ca=foreground, cb=background, aalpha=fg_alpha, balpha=bg_alpha
    // It seems balpha was used as the *opacity* of the background color, not its own alpha channel.
    // Let's assume bg_alpha is just the intensity factor for the background part.
    term2 = color_mul_scalar(background, bg_alpha * (1.0 - fg_alpha));
    return color_add(term1, term2);
}

double blend_alpha(double bg_alpha, double fg_alpha) {
     // A_o = A_f + A_b * (1 - A_f)
    return fg_alpha + bg_alpha * (1.0 - fg_alpha);
}


// sRGB Conversion Functions (from Wikipedia)
double linear_to_srgb(double v) {
    if (v <= 0.0) return 0.0; // Avoid issues with pow()
    if (v >= 1.0) return 1.0;
    if (v <= 0.0031308) {
        return 12.92 * v;
    } else {
        return 1.055 * pow(v, 1.0 / 2.4) - 0.055;
    }
}

double srgb_to_linear(double v) {
     if (v <= 0.0) return 0.0;
     if (v >= 1.0) return 1.0;
    if (v <= 0.04045) {
        return v / 12.92;
    } else {
        return pow((v + 0.055) / 1.055, 2.4);
    }
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
