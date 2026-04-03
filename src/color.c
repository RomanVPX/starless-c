#include "platform.h"
#include "color.h"
#include <math.h>
#include "core_constants.h"

const ColorRGB COLOR_BLACK = {0.0, 0.0, 0.0};
const ColorRGB COLOR_WHITE = {1.0, 1.0, 1.0};

// --- Color Operations ---
ColorRGB color_add(ColorRGB a, ColorRGB b) { return (ColorRGB){a.r + b.r, a.g + b.g, a.b + b.b}; }
ColorRGB color_mul_scalar(ColorRGB c, double s) { return (ColorRGB){c.r * s, c.g * s, c.b * s}; }
ColorRGB color_mul(ColorRGB a, ColorRGB b) { return (ColorRGB){a.r * b.r, a.g * b.g, a.b * b.b}; }

// --- ACES Tonemapping Matrices (groundtruth, fitted) ---
// Source: https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
static const double aces_input_mat[3][3] =
{
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777}
};

static const double aces_output_mat[3][3] =
{
    { 1.60475, -0.53108, -0.07367},
    {-0.10208,  1.10813, -0.00605},
    {-0.00327, -0.07276,  1.07602}
};

// --- Helper: Matrix * Vector ---
static ColorRGB mat3_mul_vec3(const double m[3][3], ColorRGB v)
{
    ColorRGB out;
    out.r = m[0][0] * v.r + m[0][1] * v.g + m[0][2] * v.b;
    out.g = m[1][0] * v.r + m[1][1] * v.g + m[1][2] * v.b;
    out.b = m[2][0] * v.r + m[2][1] * v.g + m[2][2] * v.b;
    return out;
}

// --- Helper: RRT and ODT fit (ACES) ---
static ColorRGB rrt_and_odt_fit(ColorRGB v)
{
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

ColorRGB aces_fitted(ColorRGB in)
{
    ColorRGB color = mat3_mul_vec3(aces_input_mat, in); // Input matrix
    color = rrt_and_odt_fit(color); // RRT and ODT fit
    return mat3_mul_vec3(aces_output_mat, color); // Output matrix
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
// Blends foreground OVER background USING THE PYTHON SCRIPT'S FORMULA
// !!! Note: This is NOT the standard 'over' operator! !!!
ColorRGB blend_colors(ColorRGB bg_col, double bg_alpha, ColorRGB fg_col, double fg_alpha)
{
    // Calculate background contribution term:
    ColorRGB bg_term = color_mul_scalar(bg_col, bg_alpha * (1.0 - fg_alpha));
    // Add foreground color (ca) to the background term
    return color_add(fg_col, bg_term);
} // ================================================================

ColorRGB blend_colors_over(ColorRGB bg_col, double bg_alpha, ColorRGB fg_col, double fg_alpha)
{
    double out_alpha = blend_alpha(bg_alpha, fg_alpha);
    ColorRGB background_term = color_mul_scalar(bg_col, bg_alpha);
    background_term = color_mul_scalar(background_term, 1.0 - fg_alpha);
    ColorRGB sum_of_terms = color_add(background_term, color_mul_scalar(fg_col, fg_alpha));
    return out_alpha > EPSILON_LOOSE
               ? color_mul_scalar(sum_of_terms, 1.0 / out_alpha)
               : COLOR_BLACK;
}

// Note: Arguments here follow standard bg/fg naming, but match Python calculation
double blend_alpha(double bg_alpha, double fg_alpha)
{
    return fg_alpha + bg_alpha * (1.0 - fg_alpha);
}

double linear_to_srgb(double x)
{
    if (x <= 0.0) { return 0.0; }
    if (x >= 1.0) { return 1.0; }
    if (x <= 0.0031308f) { return x * 12.92; }
    return 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

double srgb_to_linear(double x)
{
    if (x <= 0.0) { return 0.0; }
    if (x >= 1.0) { return 1.0; }
    if (x < 0.04045) { return x / 12.92; }
    return pow((x + 0.055) / 1.055, 2.4);
}

ColorRGB color_linear_to_srgb(ColorRGB c)
{
    return (ColorRGB)
    {
        linear_to_srgb(c.r),
        linear_to_srgb(c.g),
        linear_to_srgb(c.b)
    };
}

ColorRGB color_srgb_to_linear(ColorRGB c)
{
    return (ColorRGB)
    {
        srgb_to_linear(c.r),
        srgb_to_linear(c.g),
        srgb_to_linear(c.b)
    };
}

ColorRGB_u8 color_to_u8(ColorRGB c)
{
    // Assumes input color c is in the range [0, 1]
    return (ColorRGB_u8)
    {
        (uint8_t)(fmax(0.0, fmin(1.0, c.r)) * 255.999),
        (uint8_t)(fmax(0.0, fmin(1.0, c.g)) * 255.999),
        (uint8_t)(fmax(0.0, fmin(1.0, c.b)) * 255.999)
    };
}
