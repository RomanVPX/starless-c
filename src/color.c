#include "color.h"
#include "core_constants.h"
#include <math.h>
#include <stdio.h>

const ColorRGB COLOR_BLACK = {0.0, 0.0, 0.0};
const ColorRGB COLOR_WHITE = {1.0, 1.0, 1.0};

// --- Color Operations ---
ColorRGB color_add(ColorRGB a, ColorRGB b) { return (ColorRGB){a.r + b.r, a.g + b.g, a.b + b.b}; }
ColorRGB color_mul_scalar(ColorRGB c, double s) { return (ColorRGB){c.r * s, c.g * s, c.b * s}; }
ColorRGB color_mul(ColorRGB a, ColorRGB b) { return (ColorRGB){a.r * b.r, a.g * b.g, a.b * b.b}; }

// --- ACES Tonemapping Matrices (groundtruth, fitted) ---
// Source: https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
static const double ACESInputMat[3][3] = {
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777}
};

static const double ACESOutputMat[3][3] = {
    { 1.60475, -0.53108, -0.07367},
    {-0.10208,  1.10813, -0.00605},
    {-0.00327, -0.07276,  1.07602}
};

// --- Helper: Matrix * Vector ---
static ColorRGB mat3_mul_vec3(const double m[3][3], ColorRGB v) {
    ColorRGB out;
    out.r = m[0][0]*v.r + m[0][1]*v.g + m[0][2]*v.b;
    out.g = m[1][0]*v.r + m[1][1]*v.g + m[1][2]*v.b;
    out.b = m[2][0]*v.r + m[2][1]*v.g + m[2][2]*v.b;
    return out;
}

// --- Helper: RRT and ODT fit (ACES) ---
static ColorRGB RRTAndODTFit(ColorRGB v) {
    // float3 a = v * (v + 0.0245786f) - 0.000090537f;
    // float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    // return a / b;
    ColorRGB a, b, out;
    a.r = v.r * (v.r + 0.0245786) - 0.000090537;
    a.g = v.g * (v.g + 0.0245786) - 0.000090537;
    a.b = v.b * (v.b + 0.0245786) - 0.000090537;

    b.r = v.r * (0.983729 * v.r + 0.4329510) + 0.238081;
    b.g = v.g * (0.983729 * v.g + 0.4329510) + 0.238081;
    b.b = v.b * (0.983729 * v.b + 0.4329510) + 0.238081;

    // Avoid division by zero
    out.r = (b.r != 0.0) ? (a.r / b.r) : 0.0;
    out.g = (b.g != 0.0) ? (a.g / b.g) : 0.0;
    out.b = (b.b != 0.0) ? (a.b / b.b) : 0.0;
    return out;
}

ColorRGB aces_fitted(ColorRGB in) {
    // 1. Input matrix
    ColorRGB color = mat3_mul_vec3(ACESInputMat, in);
    // 2. RRT and ODT fit
    color = RRTAndODTFit(color);
    // 3. Output matrix
    color = mat3_mul_vec3(ACESOutputMat, color);
    return color;
}


ColorRGB color_clamp(ColorRGB c, double min_val, double max_val)
{
    return (ColorRGB)
    {
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
ColorRGB blend_colors(ColorRGB cb, double balpha, ColorRGB ca, double aalpha)
{
    // Calculate background contribution term: cb * (balpha * (1.0 - aalpha))
    ColorRGB background_term = color_mul_scalar(cb, balpha * (1.0 - aalpha));
    // Add foreground color (ca) to the background term
    return color_add(ca, background_term);
}
// ================================================================

ColorRGB blend_colors_over(ColorRGB cb, double balpha, ColorRGB ca, double aalpha)
{
    // over = (ca * aalpha + cb * balpha * (1.0 - aalpha))/out_alpha
    double out_alpha = blend_alpha(balpha, aalpha);
    ColorRGB background_term = color_mul_scalar(cb, balpha);
    background_term = color_mul_scalar(background_term, (1.0 - aalpha));
    ColorRGB foreground_term = color_mul_scalar(ca, aalpha);
    ColorRGB sum_of_terms = color_add(background_term, foreground_term);
    return (out_alpha > EPSILON_LOOSE)
         ? color_mul_scalar(sum_of_terms, 1.0 / out_alpha)
         : COLOR_BLACK;
}

// Assumes ca is already premultiplied by aalpha
ColorRGB blend_colors_over_premultiplied(ColorRGB cb, double balpha, ColorRGB ca, double aalpha)
{
    // over_premultiplied = ca + cb * (1.0 - aalpha)
    ColorRGB background_term = color_mul_scalar(cb, (1.0 - aalpha));
    ColorRGB sum_of_terms = color_add(background_term, ca);
    return sum_of_terms;
}

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
