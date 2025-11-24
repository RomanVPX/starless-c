#if defined(_MSC_VER)
    #define _USE_MATH_DEFINES
#endif
#define _GNU_SOURCE
#include "tracer.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "blackbody.h"
#include "color.h"
#include "config.h"
#include "core_constants.h"
#include "image.h"
#include "interpolation.h"
#include "vector.h"

#if defined(_WIN32)
    #include <windows.h>
    typedef HANDLE thread_handle_t;
    #define THREAD_FUNC_RETURN DWORD WINAPI
    #define THREAD_FUNC_CALL  __stdcall
#else
    #include <pthread.h>
    typedef pthread_t thread_handle_t;
    #define THREAD_FUNC_RETURN void *
    #define THREAD_FUNC_CALL
#endif


// --- Physics & Geometry Constants ---
#define EVENT_HORIZON_RADIUS_SQR             1.0
#define SCHWARZSCHILD_RADIUS_SQR             1.0                  // Alias for clarity
#define SINGULARITY_THRESHOLD                1e-12                // Threshold for r_sqr near singularity in RK4
#define MIN_GRAV_REDSHIFT_R_SQR              1.001                // Min r^2 for gravitational redshift calculation (avoid /0)
#define MIN_VEL_R_SQR                        0.1                  // Min r^2 for disk velocity calculation

// --- Integration & Ray Constants ---
#define OPAQUE_RAY_ALPHA_ON_STOP             false                // Set ray.alpha to 1.0 on stop
#define MAX_RAY_ALPHA                        (1 - EPSILON_STRICT) // Stop tracing if alpha exceeds this
#define MAX_DISC_ALPHA                       (1 - EPSILON_LOOSE)  // Set stop ray in disk handling if alpha exceeds this

// --- Grid Constants ---
#define GRID_PHI_STEP                        (M_PI / 6.0)         // ~0.52359... For disk grid pattern
#define GRID_HORIZON_PHI_STEP                (M_PI / 3.0)         // ~1.04719... For horizon grid pattern
#define GRID_HORIZON_ALT_STEP                (M_PI / 3.0)         // ~1.04719... For horizon grid pattern
#define GRID_ANGLE_OFFSET                    (100.0 * M_PI)       // Large offset for fmod with negative angles

// --- Blackbody & Disk Constants ---
#define DEFAULT_LOG_T0_ISCO                  9.210340371976184    // log(10000 K), default temp scale at ISCO
#define BBODY_SPEED_FACTOR                   (1.0 / M_SQRT2)      // 0.70710678... For disk velocity calculation
#define BBODY_ISCO_TAPER_FACTOR              0.3                  // Taper factor from inner disk radius
#define BBODY_TEMP_TAPER_THRESHOLD           1000.0               // Temperature threshold for outer taper

// --- Blackbody Rendering Settings ---
#define USE_ORIGINAL_OUTER_TAPER_CALCULATION false                // Use original outer taper calculation logic
#define TEMP_CUTOFF_LOW                      1000.0               // Temperature low cutoff for blackbody visibility (K)
#define TEMP_CUTOFF_HIGH                     15000.0              // Temperature high cutoff for blackbody visibility (K)

// --- Fog Constants ---
#define FOG_TAPER_FACTOR                     0.8                  // Factor for fog intensity taper near horizon

// --- Debug Single Pixel ---
#define DEBUG_SINGLE_PIXEL_MODE              false                // Set to true to enable single pixel debugging
#define DEBUG_SINGLE_PIXEL_X                 1300
#define DEBUG_SINGLE_PIXEL_Y                 950

// --- Vector/Color Constants ---
static const Vec3d VEC_3D_ZERO = {0.0, 0.0, 0.0};
static const Vec3d VEC_3D_UP = {0.0, 1.0, 0.0};


// --- RK4 Step Function ---
// Calculates the derivatives for position and velocity under the "magic potential".
// This is NOT an approximation - it's an exact reformulation of photon geodesics
// in Schwarzschild spacetime via the Binet equation, using a Newtonian-like force.
// See: http://rantonels.github.io/starless/ ('The "magic" potential' section)
// y = [pos.x, pos.y, pos.z, vel.x, vel.y, vel.z]
// dydt = [vel.x, vel.y, vel.z, accel.x, accel.y, accel.z]
static void calculate_rk4_derivs(const double y[6], double dydt[6], double h2)
{
    // Derivatives of position are just velocity
    dydt[0] = y[3];
    dydt[1] = y[4];
    dydt[2] = y[5];

    // Derivatives of velocity are acceleration
    Vec3d pos = {y[0], y[1], y[2]};
    double r_sqr = vec3d_norm_sqr(pos);

    // Avoid division by zero if r_sqr is tiny
    if (r_sqr < SINGULARITY_THRESHOLD)
    { // Close to singularity
        dydt[3] = 0.0;
        dydt[4] = 0.0;
        dydt[5] = 0.0;
        // Or maybe set velocity to zero / mark ray as stopped?
    }
    else
    {
        // Accel = -1.5 * h^2 * r_vec / r^5
        double r_pow_neg5 = pow(r_sqr, -2.5); // r^-5 = (r^2)^(-5/2)
        double factor = -1.5 * h2 * r_pow_neg5;
        Vec3d accel = vec3d_mul_scalar(pos, factor);
        dydt[3] = accel.x;
        dydt[4] = accel.y;
        dydt[5] = accel.z;
    }
}

// --- Single RK4 Step ---
static void perform_rk4_step(RayState *ray, double step_size, bool distort)
{
    if (!ray->active) return;
    if (!distort)
    { // Simple Euler step if no distortion (straight line)
        ray->pos = vec3d_add(ray->pos, vec3d_mul_scalar(ray->vel, step_size));
        // Velocity remains constant
    }
    else
    { // Standard RK4 integration
        double y[6] = {ray->pos.x, ray->pos.y, ray->pos.z, ray->vel.x, ray->vel.y, ray->vel.z};
        double k1[6], k2[6], k3[6], k4[6];
        double temp_y[6];
        double h = step_size;

        // Calculate k1
        calculate_rk4_derivs(y, k1, ray->h2);
        // Calculate k2
        for (int i = 0; i < 6; ++i) temp_y[i] = y[i] + 0.5 * h * k1[i];
        calculate_rk4_derivs(temp_y, k2, ray->h2);
        // Calculate k3
        for (int i = 0; i < 6; ++i) temp_y[i] = y[i] + 0.5 * h * k2[i];
        calculate_rk4_derivs(temp_y, k3, ray->h2);
        // Calculate k4
        for (int i = 0; i < 6; ++i) temp_y[i] = y[i] + h * k3[i];
        calculate_rk4_derivs(temp_y, k4, ray->h2);
        // Update y using weighted average of ks
        for (int i = 0; i < 6; ++i) { y[i] += (h / 6.0) * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]); }
        // Update ray state
        ray->pos = (Vec3d){y[0], y[1], y[2]};
        ray->vel = (Vec3d){y[3], y[4], y[5]};
    }
    ray->steps_taken++;
}

// --- Helper: Calculate initial ray direction in world space ---
static Vec3d calculate_initial_view_vector(int px, int py, double sub_pixel_offset_x, double sub_pixel_offset_y, const Config *cfg)
{
    int W = cfg->resolution[0];
    int H = cfg->resolution[1];

    double screen_x = ((double)px + sub_pixel_offset_x) / W - 0.5;
    double screen_y = (-(((double)py + sub_pixel_offset_y) / H) + 0.5) * ((double)H / W);

    screen_x *= cfg->tan_fov;
    screen_y *= cfg->tan_fov;
    Vec3d view_cam_space = {screen_x, screen_y, 1.0};
    Vec3d view_world = VEC_3D_ZERO;
    view_world = vec3d_add(view_world, vec3d_mul_scalar(cfg->view_matrix[0], view_cam_space.x));
    view_world = vec3d_add(view_world, vec3d_mul_scalar(cfg->view_matrix[1], view_cam_space.y));
    view_world = vec3d_add(view_world, vec3d_mul_scalar(cfg->view_matrix[2], view_cam_space.z));
    return vec3d_normalize(view_world);
}

// --- Helper: Initialize the ray state ---
static void initialize_ray_state(RayState *ray, Vec3d initial_velocity, const Config *cfg)
{
    ray->pos = cfg->camera_pos;
    ray->vel = initial_velocity;
    ray->initial_vel = initial_velocity; // Store for sky lookup
    ray->color = COLOR_BLACK;
    ray->alpha = 0.0;
    ray->active = true;
    ray->steps_taken = 0;
    // Calculate h^2 (squared specific angular momentum)
    Vec3d initial_momentum = vec3d_cross(ray->pos, ray->vel);
    ray->h2 = vec3d_norm_sqr(initial_momentum);
}


static double calculate_disk_color_pattern(const Vec3d col_point, double R, const Config *cfg)
{
    double phi = atan2(col_point.x, col_point.z);
    double normalized_r = (R - cfg->disk_inner_radius) / (cfg->disk_outer_radius - cfg->disk_inner_radius);

    // Spiral pattern parameters
    int spiral_arms = cfg->disk_structure_spiral_arms;
    double spiral_pitch = cfg->disk_structure_spiral_pitch;
    double spiral_pattern = sin(spiral_arms * phi + R * spiral_pitch) * 0.5 + 0.5;

    Vec3d freqs = cfg->disk_structure_rings_freq;
    // Combine ring patterns
    double ring_thin = sin(normalized_r * 16.0 * freqs.x * M_PI) * 0.5 + 0.5;
    double ring_medium = sin(normalized_r * 8.0 * freqs.y * M_PI) * 0.5 + 0.5;
    double ring_thick = sin(normalized_r * 4.0 * freqs.z * M_PI) * 0.5 + 0.5;

    double position_variation = sin(phi * 7.0 + R * 3.0) * cfg->disk_structure_position_variation + 1.0;

    double ring_mixed = fabs(ring_thin - ring_medium * position_variation) * 0.6 + 0.5; // Additional mixed pattern

    ring_thin = smoothstep(0.45, 0.55 + position_variation, ring_thin) * (0.5 + position_variation);
    ring_medium = smoothstep(0.3, 0.7, ring_medium) * (0.8 - (spiral_pattern * 0.2));
    ring_thick = smoothstep(0.1 + spiral_pattern * 0.14, 0.9 - spiral_pattern * 0.1, ring_thick);

    ring_mixed = smoothstep(0.2, 0.8, ring_mixed) * (0.5 + position_variation * 0.5);

    double radial_intensity = 1.0 + 0.75 * (1.0 - normalized_r); // Fade out towards the outer edge

    double combined_rings = fabs(ring_thin + ring_medium + ring_thick - ring_mixed);
    double final_pattern = (spiral_pattern * 0.4 + combined_rings * 0.6) * radial_intensity;

    double intensity_modulation = 1.0 + cfg->disk_structure_modulation * (final_pattern - 1.0);
    return clamp(intensity_modulation, 1.0 - cfg->disk_structure_modulation, 1.0 + cfg->disk_structure_modulation); // Limit modulation
}

// --- Helper: Handle Accretion Disk Hit ---
// Determines color and alpha based on disk mode and blends it.
// Returns true if the ray should stop after this hit.
static bool handle_disk_hit(RayState *ray, const Vec3d col_point, double col_point_sqr, const Config *cfg, bool log_this_pixel)
{
    ColorRGB disk_color = COLOR_BLACK;
    double disk_alpha = 0.0;
    bool stop_ray = false;

    switch (cfg->disk_texture_mode)
    {
        case DT_GRID:
        {
            double phi = atan2(col_point.x, col_point.z);
            bool phi_check = fmod(phi + GRID_ANGLE_OFFSET, GRID_PHI_STEP * 2.0) < GRID_PHI_STEP; // Check pi/6 step
            disk_color = phi_check ? (ColorRGB){1.0, 1.0, 1.0} : (ColorRGB){0.0, 0.0, 1.0};
            disk_alpha = 1.0;
            stop_ray = true; // Grid is opaque
            break;
        }
        case DT_SOLID:
        {
            disk_color = (ColorRGB){1.0, 1.0, 0.98};
            disk_alpha = 1.0;
            stop_ray = true; // Solid disk is opaque
            break;
        }
        case DT_TEXTURE:
            if (cfg->disk_texture)
            {
                double phi = atan2(col_point.x, col_point.z);
                double r = sqrt(col_point_sqr);
                double u = fmod(phi + 2.0 * M_PI, 2.0 * M_PI) / (2.0 * M_PI);                                // Normalize phi [0, 1]
                double v = (r - cfg->disk_inner_radius) / (cfg->disk_outer_radius - cfg->disk_inner_radius); // Normalize radius

                disk_color = texture_lookup(cfg->disk_texture, u, v, cfg->srgb_in);
                double color_norm_sq = vec3d_norm_sqr(*(Vec3d *)&disk_color);
                disk_alpha = fmax(0.0, fmin(1.0, color_norm_sq / 3.0));

                // Stop if alpha is high enough (mimicking original code's implicit stop)
                if (disk_alpha >= MAX_DISC_ALPHA) { stop_ray = true; }

                if (log_this_pixel)
                {
                    printf("--- Mode=DT_TEXTURE\n");
                    printf("--- Collision Point: (%.3f, %.3f, %.3f)\n", col_point.x, col_point.y, col_point.z);
                    printf("--- phi=%.4f, r=%.4f\n", phi, r);
                    printf("--- UV=(%.4f, %.4f)\n", u, v);
                    printf("--- Looked up color (linear): (%.3f, %.3f, %.3f)\n", disk_color.r, disk_color.g, disk_color.b);
                    printf("--- Color norm^2=%.4f\n", color_norm_sq);
                    printf("--- Resulting disk_alpha=%.4f\n", disk_alpha);
                }
            }
            else
            {
                disk_alpha = 0.0; // No texture loaded
                if (log_this_pixel) printf("--- Disk texture not loaded!\n");
            }
            break;
        case DT_BLACKBODY:
        {
            double log_temp = bb_log_temperature(col_point_sqr, DEFAULT_LOG_T0_ISCO);
            double temp = exp(log_temp);
            double R = sqrt(col_point_sqr);

            if (cfg->redshift != 1.0 && R > sqrt(MIN_GRAV_REDSHIFT_R_SQR)) // Apply redshift if enabled and outside horizon slightly
            {
                // Formula from Python code for velocity factor
                double speed_factor = BBODY_SPEED_FACTOR * pow(fmax(MIN_VEL_R_SQR, R - sqrt(SCHWARZSCHILD_RADIUS_SQR)), -0.5);
                Vec3d disk_vel_dir = vec3d_cross(VEC_3D_UP, vec3d_normalize(col_point));
                Vec3d disk_vel = vec3d_mul_scalar(disk_vel_dir, speed_factor);
                double disk_vel_sqr = fmin(0.99, vec3d_norm_sqr(disk_vel)); // Clamp speed < c

                double gamma = 1.0 / sqrt(1.0 - disk_vel_sqr);
                double doppler_dot = vec3d_dot(disk_vel, vec3d_normalize(ray->vel));
                double opz_doppler = gamma * (1.0 - doppler_dot); // 1+z, NOTE: '-' sign used here based on standard physics

                double opz_grav = 1.0 / sqrt(fmax(EPSILON_LOOSE,
                                                  1.0 - sqrt(SCHWARZSCHILD_RADIUS_SQR) / R)); // 1+z grav sqrt(g_tt)

                double total_opz = opz_doppler * opz_grav * cfg->redshift;
                temp /= fmax(0.1, total_opz); // Correct temperature

                if (log_this_pixel)
                {
                    printf("--- Mode=DT_BLACKBODY\n");
                    printf("--- Collision Point: (%.3f, %.3f, %.3f)\n", col_point.x, col_point.y, col_point.z);
                    printf("--- R=%.4f, log_temp=%.4f, temp=%.4f\n", R, log_temp, temp);
                    printf("--- Doppler factor: %.4f, Gravitational factor: %.4f, Total opz: %.4f\n", opz_doppler, opz_grav, total_opz);
                }
            }

            ColorRGB bb_col = bb_color_from_temp(cfg, temp);

            // --- Apply multiplier ---
            if (cfg->disk_intensity_do) { disk_color = color_mul_scalar(bb_col, cfg->disk_multiplier); }
            else { disk_color = bb_col; }

            // --- Add structure if enabled ---
            if (cfg->disk_add_structure) { disk_color = color_mul_scalar(disk_color, calculate_disk_color_pattern(col_point, R, cfg)); }

            // --- Alpha calculation ---
            double isco_taper = saturate((col_point_sqr - cfg->disk_inner_sqr) * BBODY_ISCO_TAPER_FACTOR);
            double outer_taper = saturate(temp / BBODY_TEMP_TAPER_THRESHOLD);
#if !USE_ORIGINAL_OUTER_TAPER_CALCULATION
            // outer_taper *= smoothstep(cfg->disk_outer_sqr, lerp(cfg->disk_inner_sqr, cfg->disk_outer_sqr, 0.75), col_point_sqr);
            outer_taper *= smoothstep(cfg->disk_outer_sqr * 0.95, cfg->disk_outer_sqr * 0.85, col_point_sqr);
#endif
            disk_alpha = isco_taper * outer_taper;
            if (disk_alpha >= MAX_DISC_ALPHA) { stop_ray = true; }
            break;
        }
        default:
            break;
    }

    // Blend disk color (cb=disk, ca=ray)
    if (log_this_pixel && disk_alpha > EPSILON_LOOSE)
    {
        printf("--- Blending Disk: Ray (%.3f,%.3f,%.3f a=%.3f) + Disk (%.3f,%.3f,%.3f a=%.3f)\n", ray->color.r, ray->color.g, ray->color.b,
               ray->alpha, disk_color.r, disk_color.g, disk_color.b, disk_alpha);
    }
    ray->color = BLEND_COLORS(disk_color, disk_alpha, ray->color, ray->alpha);
    ray->alpha = blend_alpha(disk_alpha, ray->alpha);

    if (log_this_pixel && disk_alpha > EPSILON_LOOSE)
    {
        printf("--- Result: (%.3f,%.3f,%.3f a=%.3f)\n", ray->color.r, ray->color.g, ray->color.b, ray->alpha);
    }

    /* mark ray opaque if we decided to stop */
    if (stop_ray && OPAQUE_RAY_ALPHA_ON_STOP) { ray->alpha = 1.0; }
    return stop_ray;
}


// --- Helper: Handle Event Horizon Hit ---
static void handle_horizon_hit(RayState *ray, const Vec3d old_pos, double old_pos_sqr, const Config *cfg, bool log_this_pixel)
{
    double alpha_before_hit = ray->alpha; // Store alpha before overwrite

    if (log_this_pixel)
    {
        printf("--- Iter %d: EVENT HORIZON HIT DETECTED!\n", ray->steps_taken);
        printf("--- Previous color was: (%.3f,%.3f,%.3f a=%.3f)\n", ray->color.r, ray->color.g, ray->color.b, alpha_before_hit);
    }

    if (alpha_before_hit >= MAX_DISC_ALPHA)
    {
        if (log_this_pixel)
        {
            printf("--- Ray already had alpha %.3f (> 0.1), PRESERVING color, ignoring horizon overwrite.\n", alpha_before_hit);
        }

        if (OPAQUE_RAY_ALPHA_ON_STOP) { ray->alpha = 1.0; }
        ray->active = false;
        return; // Exit without blending horizon color
    }

    // Otherwise (ray was transparent), apply horizon color (black or grid)
    if (log_this_pixel) { printf("--- Ray was transparent (alpha=%.3f <= 0.1), applying horizon color.\n", alpha_before_hit); }

    ColorRGB horizon_color = COLOR_BLACK;
    if (cfg->horizon_grid)
    {
        // Interpolate collision point lambda to get horizon color
        double r_old = sqrt(fmax(1e-9, old_pos_sqr));                     // Avoid sqrt(0)
        double phi = atan2(old_pos.x, old_pos.z);
        double altitude = asin(fmax(-1.0, fmin(1.0, old_pos.y / r_old))); // Altitude angle

        bool phi_check = fmod(phi + GRID_ANGLE_OFFSET, GRID_HORIZON_PHI_STEP) < (GRID_HORIZON_PHI_STEP * 0.5);
        bool alt_check = fmod(altitude + M_PI / 2.0 + GRID_ANGLE_OFFSET, GRID_HORIZON_ALT_STEP) < (GRID_HORIZON_ALT_STEP * 0.5);

        if (phi_check ^ alt_check) { horizon_color = (ColorRGB){1.0, 0.0, 0.0}; }
    }

    double horizon_alpha = 1.0; // Opaque horizon

    // Blend horizon color (cb=horizon, ca=ray)
    ray->color = BLEND_COLORS(horizon_color, horizon_alpha, ray->color, alpha_before_hit);
    ray->alpha = blend_alpha(horizon_alpha, alpha_before_hit); // Will become 1.0

    ray->active = false;                                       // Stop tracing this ray
    if (OPAQUE_RAY_ALPHA_ON_STOP) { ray->alpha = 1.0; }        // Opaque – prevent further blending
}


// --- Helper: Apply Fog ---
static void apply_fog(RayState *ray, double current_pos_sqr, const Config *cfg)
{
    if (!cfg->fog_do || (ray->steps_taken % cfg->fog_skip != 0)) { return; } // Fog disabled or skip this step
    if (current_pos_sqr <= SCHWARZSCHILD_RADIUS_SQR) { return; } // No fog inside horizon

    double phsphtaper = fmax(0.0, fmin(1.0, FOG_TAPER_FACTOR * (current_pos_sqr - SCHWARZSCHILD_RADIUS_SQR)));
    double fog_int_base = cfg->fog_mult * cfg->fog_skip * cfg->step_size / fmax(1e-6, current_pos_sqr);
    double fog_alpha_step = fmax(0.0, fmin(1.0, fog_int_base)) * phsphtaper;
    ColorRGB fog_col = COLOR_WHITE; // Fog color is white

    // Blend fog (cb=fog, ca=ray)
    ray->color = BLEND_COLORS(fog_col, fog_alpha_step, ray->color, ray->alpha);
    ray->alpha = blend_alpha(fog_alpha_step, ray->alpha);
}

// --- Helper: Get Background Sky Color ---
static ColorRGB get_background_color(const RayState *ray, const Config *cfg)
{
    ColorRGB bg_color = COLOR_BLACK; // Default background

    // Use final velocity direction for sky lookup
    Vec3d final_vel_norm = vec3d_normalize(ray->vel);

    // Calculate spherical coordinates (phi, theta/altitude) from final velocity
    double vphi = atan2(final_vel_norm.x, final_vel_norm.z); // Azimuth
    // Altitude (angle from xz-plane, asin(y)): range [-pi/2, pi/2]
    double valtitude = asin(fmax(-1.0, fmin(1.0, final_vel_norm.y)));

    // Map angles to UV [0, 1]
    // Using standard mapping here unless proven necessary
    double sky_u = fmod(vphi + 2.0 * M_PI, 2.0 * M_PI) / (2.0 * M_PI); // Longitude [0, 1]
    // Map altitude [-pi/2, pi/2] to v [0, 1]
    double sky_v = (valtitude + 0.5 * M_PI) / M_PI;

    switch (cfg->sky_texture_mode)
    {
        case ST_TEXTURE:
            if (cfg->sky_texture)
            {
                bg_color = texture_lookup(cfg->sky_texture, sky_u, sky_v, cfg->srgb_in);
            } // else bg_color remains black
            break;
        case ST_FINAL: // Debug mode: color based on final direction component magnitudes
            bg_color = (ColorRGB){fabs(final_vel_norm.x), fabs(final_vel_norm.y), fabs(final_vel_norm.z)};
            break;
        case ST_NONE: // Fallthrough
        default:
            break;    // bg_color is already black
    }
    // Apply sky brightness scaling AFTER lookup/calculation
    return color_mul_scalar(bg_color, cfg->sky_disk_ratio);
}


// --- Trace for one pixel function ---
static ColorRGB trace_pixel(int px, int py, double sub_pixel_offset_x, double sub_pixel_offset_y, const Config *cfg, int sample_idx_for_log)
{
    bool log_this_pixel = (DEBUG_SINGLE_PIXEL_MODE && px == DEBUG_SINGLE_PIXEL_X && py == DEBUG_SINGLE_PIXEL_Y);
    if (log_this_pixel)
    {
        printf("\n--- Logging for pixel (%d, %d), SAMPLE %d\n", px, py, sample_idx_for_log);
    }

    // 1. Initialize Ray
    Vec3d initial_vel = calculate_initial_view_vector(px, py, sub_pixel_offset_x, sub_pixel_offset_y, cfg);
    RayState ray;
    initialize_ray_state(&ray, initial_vel, cfg);

    // 2. Integration Loop
    Vec3d old_pos;
    double old_pos_sqr;

    for (int it = 0; it < cfg->n_iterations; ++it)
    {
        if (!ray.active || ray.alpha >= MAX_RAY_ALPHA) break;

        old_pos = ray.pos;
        old_pos_sqr = vec3d_norm_sqr(old_pos);
        // --- Step ---
        perform_rk4_step(&ray, cfg->step_size, cfg->distort);
        double current_pos_sqr = vec3d_norm_sqr(ray.pos);
        // --- Check for Horizon Hit ---
        if (old_pos_sqr > SCHWARZSCHILD_RADIUS_SQR && current_pos_sqr <= SCHWARZSCHILD_RADIUS_SQR)
        {
            handle_horizon_hit(&ray, old_pos, old_pos_sqr, cfg, log_this_pixel);
            continue; // Skip disk/fog checks if horizon was hit this step
        }
        // --- Check for Disk Hit ---
        if (cfg->disk_texture_mode != DT_NONE && (old_pos.y * ray.pos.y < 0.0))
        { // Crossed y=0 plane
            double delta_y = ray.pos.y - old_pos.y;
            if (fabs(delta_y) > EPSILON_STRICT)
            { // Avoid division by zero if static on plane
                double t_cross = -old_pos.y / delta_y;
                if (t_cross >= -EPSILON_LOOSE && t_cross <= 1.0 + EPSILON_LOOSE)
                { // Intersection within step
                    t_cross = fmax(0.0, fmin(1.0, t_cross));
                    Vec3d col_point = vec3d_add(old_pos, vec3d_mul_scalar(vec3d_sub(ray.pos, old_pos), t_cross));
                    double col_point_sqr = vec3d_norm_sqr(col_point);
                    if (col_point_sqr >= cfg->disk_inner_sqr && col_point_sqr <= cfg->disk_outer_sqr)
                    { // Within disk radial bounds
                        bool stop_after_disk = handle_disk_hit(&ray, col_point, col_point_sqr, cfg, log_this_pixel);
                        if (stop_after_disk)
                        {
                            ray.active = false;
                            if (OPAQUE_RAY_ALPHA_ON_STOP) { ray.alpha = 1.0; } /* fully opaque – skip sky */
                            if (log_this_pixel) printf("--- Ray stopped after disk hit.\n");
                        }
                    }
                }
            }
        }

        // --- Apply Fog ---
        // Apply fog *after* disk/horizon checks for this step
        apply_fog(&ray, current_pos_sqr, cfg);
    } // End integration loop

    if (log_this_pixel)
    {
        printf("--- Loop finished. Accumulated color: (%.3f,%.3f,%.3f a=%.3f)\n", ray.color.r, ray.color.g, ray.color.b, ray.alpha);
    }

    // 3. Background / Sky Color Blending
    if (ray.alpha < MAX_RAY_ALPHA)
    {                            // If ray didn't hit something fully opaque
        ColorRGB bg_color = get_background_color(&ray, cfg);
        double sky_balpha = 1.0; // Sky is opaque background
        // Blend sky (cb=sky, ca=ray)
        ray.color = BLEND_COLORS(bg_color, sky_balpha, ray.color, ray.alpha);
        ray.alpha = blend_alpha(sky_balpha, ray.alpha); // Final alpha should be 1.0
    }

    if (log_this_pixel) { printf("--- Final pixel color: (%.3f,%.3f,%.3f)\n", ray.color.r, ray.color.g, ray.color.b); }

    return ray.color;
}


// --- Thread-safe random number generator (LCG) ---
static unsigned int thread_safe_rand(unsigned int *seed)
{
    // LCG parameters are taken from POSIX rand_r
    *seed = (*seed * 1103515245u + 12345u) & 0x7fffffff;
    return *seed;
}

// --- Trace a range of pixels (for threading) ---
THREAD_FUNC_RETURN THREAD_FUNC_CALL trace_pixel_range(void *thread_arg)
{
    ThreadData *data = (ThreadData *)thread_arg;
    const Config *cfg = data->config;
    ImageF *image = data->image;
    int W = image->width;

    printf("  Thread %d: Tracing pixels %d to %d\n", data->thread_id, data->start_pixel_index, data->end_pixel_index);
    struct timespec ts_start;
    timespec_get(&ts_start, TIME_UTC);

    for (int idx = data->start_pixel_index; idx < data->end_pixel_index; ++idx)
    {
        int px = idx % W;
        int py = idx / W;

        ColorRGB accumulated_color = COLOR_BLACK;
        int num_samples_axis = cfg->ssaa_level > 0 ? cfg->ssaa_level : 1;
        int total_samples = num_samples_axis * num_samples_axis;

        if (total_samples == 1)
        { // SSAA is 1x1
            // No jittering, just center sample
            accumulated_color = trace_pixel(px, py, 0.5, 0.5, cfg, 0);
        }
        else
        {
            for (int sy = 0; sy < num_samples_axis; ++sy)
            {
                for (int sx = 0; sx < num_samples_axis; ++sx)
                {
                    // Jittered stratified grid
                    double jitter_x = (double)thread_safe_rand(&data->rand_seed) / (double)RAND_MAX;
                    double jitter_y = (double)thread_safe_rand(&data->rand_seed) / (double)RAND_MAX;

                    // Sub-pixel offsets
                    double sub_pixel_offset_x = (sx + jitter_x) / num_samples_axis;
                    double sub_pixel_offset_y = (sy + jitter_y) / num_samples_axis;

                    int current_sample_idx = sy * num_samples_axis + sx; // For logging of the first sample
                    ColorRGB sample_color = trace_pixel(px, py, sub_pixel_offset_x, sub_pixel_offset_y, cfg, current_sample_idx);
                    accumulated_color = color_add(accumulated_color, sample_color);
                }
            }
            accumulated_color = color_mul_scalar(accumulated_color, 1.0 / total_samples);
        }
        image->pixels[idx] = accumulated_color;
    }

    struct timespec ts_end;
    timespec_get(&ts_end, TIME_UTC);
    double time_spent = (ts_end.tv_sec - ts_start.tv_sec) + (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000000.0;
    printf("  Thread %d: Finished range in %.2f seconds.\n", data->thread_id, time_spent);
    return 0;
}


// --- Main run_tracer Function ---
bool run_tracer(Config *config, ImageF *output_image)
{
    if (!config || !output_image || !output_image->pixels)
    {
        fprintf(stderr, "! Error: Invalid arguments passed to run_tracer.\n");
        return false;
    }

    int W = output_image->width;
    int H = output_image->height;
    int num_pixels = W * H;
    int n_threads = config->n_threads;

    if (n_threads <= 0) n_threads = 1; // Ensure at least one thread

    int samples_per_axis = (config->ssaa_level > 0) ? config->ssaa_level : 1;
    int total_samples = samples_per_axis * samples_per_axis;
    printf("  Starting ray tracing with %d threads...\n", n_threads);
    printf("  SSAA: %dx%d = %d samples/pixel\n", samples_per_axis, samples_per_axis, total_samples);
    printf("  Total pixels: %d\n", num_pixels);

    // Allocate thread handles and data structures
    thread_handle_t *threads = (thread_handle_t *)malloc(n_threads * sizeof(thread_handle_t));
    ThreadData *thread_data = (ThreadData *)malloc(n_threads * sizeof(ThreadData));

    if (!threads || !thread_data)
    {
        fprintf(stderr, "! Error: Failed to allocate memory for thread management.\n");
        free(threads);
        free(thread_data);
        return false;
    }

    // Divide work among threads
    int pixels_per_thread = num_pixels / n_threads;
    int remaining_pixels = num_pixels % n_threads;
    int current_pixel_index = 0;

    clock_t total_start_time = clock();

    for (int i = 0; i < n_threads; ++i)
    {
        thread_data[i].thread_id = i;
        thread_data[i].config = config;
        thread_data[i].rand_seed = (unsigned int)(time(NULL) + i); // Unique seed per thread
        thread_data[i].image = output_image;
        thread_data[i].start_pixel_index = current_pixel_index;

        int pixels_for_this_thread = pixels_per_thread + (i < remaining_pixels ? 1 : 0);
        thread_data[i].end_pixel_index = current_pixel_index + pixels_for_this_thread;

        current_pixel_index += pixels_for_this_thread;

        // Create thread
#if defined(_WIN32)
        threads[i] = CreateThread(NULL, 0, trace_pixel_range, &thread_data[i], 0, NULL);
        if (threads[i] == NULL)
        {
            fprintf(stderr, "! Error creating thread %d\n", i);
            n_threads = i;
            break;
        }
#else
        int ret = pthread_create(&threads[i], NULL, trace_pixel_range, &thread_data[i]);
        if (ret != 0)
        {
            fprintf(stderr, "! Error creating thread %d: %s\n", i, strerror(ret));
            n_threads = i;
            break;
        }
#endif
    }
    int threads_failed = 0;
    for (int i = 0; i < n_threads; ++i)
    {
#if defined(_WIN32)
        DWORD wait_result = WaitForSingleObject(threads[i], INFINITE);
        if (wait_result != WAIT_OBJECT_0)
        {
            fprintf(stderr, "! Error joining thread %d\n", i);
            threads_failed++;
        }
        CloseHandle(threads[i]);
#else
        int ret = pthread_join(threads[i], NULL);
        if (ret != 0)
        {
            fprintf(stderr, "! Error joining thread %d: %s\n", i, strerror(ret));
            threads_failed++;
        }
#endif
    }

    clock_t total_end_time = clock();
    double total_time_spent = (double)(total_end_time - total_start_time) / CLOCKS_PER_SEC;

    // Cleanup thread resources
    free(threads);
    free(thread_data);

    if (threads_failed > 0)
    {
        // Let's pretend it's not a failure
        fprintf(stderr, "  WARNING: %d threads failed to join correctly.\n", threads_failed);
        // return false;
    }

    printf("  All threads finished. Total ray tracing time: %.2f seconds.\n", total_time_spent);
    printf("  Final image size: %d x %d\n", W, H);
    printf("  Total pixels processed: %d\n", num_pixels);
    printf("  Average time per pixel: %.6f ms\n", total_time_spent / num_pixels * 1000.0);
    return true;
}
