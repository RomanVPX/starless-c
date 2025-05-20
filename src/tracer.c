#include "tracer.h"
#include "vector.h"
#include "color.h"
#include "core_constants.h"
#include "config.h" // Already included via tracer.h
#include "image.h"  // Already included via tracer.h
#include "blackbody.h" // For disk blackbody calculations
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h> // For threading later
#include <string.h>
#include <time.h>    // For progress timing later


// --- Physics & Geometry Constants ---
#define EVENT_HORIZON_RADIUS_SQR 1.0
#define SCHWARZSCHILD_RADIUS_SQR 1.0  // Alias for clarity
#define SINGULARITY_THRESHOLD 1e-12   // Threshold for r_sqr near singularity in RK4
#define MIN_GRAV_REDSHIFT_R_SQR 1.001 // Min r^2 for gravitational redshift calculation (avoid division by zero)
#define MIN_VEL_R_SQR 0.1             // Min r^2 for disk velocity calculation

// --- Integration & Ray Constants ---
#define MAX_RAY_ALPHA 0.9999        // Stop tracing if alpha exceeds this
#define CROSSING_TOLERANCE 1e-6     // Tolerance for checking disk/plane crossing parameter t

// --- Grid Constants ---
#define GRID_PHI_STEP (M_PI / 6.0)         // ~0.52359... For disk grid pattern
#define GRID_HORIZON_PHI_STEP (M_PI / 3.0) // ~1.04719... For horizon grid pattern
#define GRID_HORIZON_ALT_STEP (M_PI / 3.0) // ~1.04719... For horizon grid pattern
#define GRID_ANGLE_OFFSET (100.0 * M_PI)   // Large offset for fmod with negative angles

// --- Blackbody & Disk Constants ---
#define DEFAULT_LOG_T0_ISCO 9.210340371976184 // log(10000 K), default temp scale at ISCO
#define BBODY_SPEED_FACTOR (1.0 / M_SQRT2)    // 0.70710678... For disk velocity calculation
#define BBODY_ISCO_TAPER_FACTOR 0.3           // Taper factor from inner disk radius
#define BBODY_TEMP_TAPER_THRESHOLD 1000.0     // Temperature threshold for outer taper
#define BBODY_MAX_CLAMP_VALUE 100.0           // Max color value clamp for blackbody

// --- Fog Constants ---
#define FOG_TAPER_FACTOR 0.8 // Factor for fog intensity taper near horizon

// --- Debug Single Pixel ---
#define DEBUG_SINGLE_PIXEL_X 1300
#define DEBUG_SINGLE_PIXEL_Y 950

// --- Vector/Color Constants ---
static const Vec3d VEC3D_ZERO = {0.0, 0.0, 0.0};
static const Vec3d VEC3D_UP = {0.0, 1.0, 0.0};


// --- RK4 Step Function ---
// Calculates the derivatives for position and velocity under the approximate potential.
// y = [pos.x, pos.y, pos.z, vel.x, vel.y, vel.z]
// dydt = [vel.x, vel.y, vel.z, accel.x, accel.y, accel.z]
static void calculate_rk4_derivs(const double y[6], double dydt[6], double h2) {
    // Derivatives of position are just velocity
    dydt[0] = y[3];
    dydt[1] = y[4];
    dydt[2] = y[5];

    // Derivatives of velocity are acceleration
    Vec3d pos = {y[0], y[1], y[2]};
    double r_sqr = vec3d_norm_sqr(pos);

    // Avoid division by zero if r_sqr is tiny
    if (r_sqr < 1e-12) { // Close to singularity
        dydt[3] = 0.0;
        dydt[4] = 0.0;
        dydt[5] = 0.0;
        // Or maybe set velocity to zero / mark ray as stopped?
    } else {
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
static void perform_rk4_step(RayState *ray, double step_size, bool distort) {
    if (!ray->active) return;

    if (!distort) {
        // Simple Euler step if no distortion (straight line)
        ray->pos = vec3d_add(ray->pos, vec3d_mul_scalar(ray->vel, step_size));
        // Velocity remains constant
    } else {
        // Standard RK4 integration
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
        // y_new = y + (h/6) * (k1 + 2*k2 + 2*k3 + k4)
        for (int i = 0; i < 6; ++i) {
            y[i] += (h / 6.0) * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]);
        }

        // Update ray state
        ray->pos = (Vec3d){y[0], y[1], y[2]};
        ray->vel = (Vec3d){y[3], y[4], y[5]};
    }
    ray->steps_taken++;
}


// --- Helper: Calculate initial ray direction in world space ---
static Vec3d calculate_initial_view_vector(int px, int py, const Config *cfg) {
    int W = cfg->resolution[0];
    int H = cfg->resolution[1];

    // Screen coordinates [-0.5, 0.5] for x, corrected aspect for y
    double screen_x = (((double)px + 0.5) / W) - 0.5;
    double screen_y = (-(((double)py + 0.5) / H) + 0.5) * ((double)H / W); // Aspect ratio correction

    screen_x *= cfg->tan_fov;
    screen_y *= cfg->tan_fov;

    // Form vector in camera space (Z=1)
    Vec3d view_cam_space = {screen_x, screen_y, 1.0};

    // Rotate into world space using view matrix (matrix * vector)
    Vec3d view_world = VEC3D_ZERO;
    view_world = vec3d_add(view_world, vec3d_mul_scalar(cfg->view_matrix[0], view_cam_space.x)); // Left * x
    view_world = vec3d_add(view_world, vec3d_mul_scalar(cfg->view_matrix[1], view_cam_space.y)); // Up * y
    view_world = vec3d_add(view_world, vec3d_mul_scalar(cfg->view_matrix[2], view_cam_space.z)); // Front * z

    return vec3d_normalize(view_world);
}

// --- Helper: Initialize the ray state ---
static void initialize_ray_state(RayState *ray, Vec3d initial_velocity, const Config *cfg) {
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

// --- Helper: Handle Accretion Disk Hit ---
// Determines color and alpha based on disk mode and blends it.
// Returns true if the ray should stop after this hit.
static bool handle_disk_hit(RayState *ray, const Vec3d col_point, double col_point_sqr, const Config *cfg, bool log_this_pixel) {
    ColorRGB disk_color = COLOR_BLACK;
    double disk_alpha = 0.0;
    bool stop_ray = false;

    switch (cfg->disk_texture_mode) {
        case DT_GRID: {
            double phi = atan2(col_point.x, col_point.z);
            bool phi_check = fmod(phi + GRID_ANGLE_OFFSET, GRID_PHI_STEP * 2.0) < GRID_PHI_STEP; // Check pi/6 step
            disk_color = phi_check ? (ColorRGB){1.0, 1.0, 1.0} : (ColorRGB){0.0, 0.0, 1.0};
            disk_alpha = 1.0;
            stop_ray = true; // Grid is opaque
            break;
        }
        case DT_SOLID:
            disk_color = (ColorRGB){1.0, 1.0, 0.98};
            disk_alpha = 1.0;
            stop_ray = true; // Solid disk is opaque
            break;
        case DT_TEXTURE:
            if (cfg->disk_texture) {
                double phi = atan2(col_point.x, col_point.z);
                double r = sqrt(col_point_sqr);
                double u = fmod(phi + 2.0 * M_PI, 2.0 * M_PI) / (2.0 * M_PI); // Normalize phi [0, 1]
                double v = (r - cfg->disk_inner_radius) / (cfg->disk_outer_radius - cfg->disk_inner_radius); // Normalize radius

                disk_color = texture_lookup(cfg->disk_texture, u, v, cfg->srgb_in);
                double color_norm_sq = vec3d_norm_sqr(*(Vec3d *)&disk_color);
                disk_alpha = fmax(0.0, fmin(1.0, color_norm_sq / 3.0));

                // Stop if alpha is very high (mimicking original code's implicit stop)
                if (disk_alpha >= 0.95) { // Adjusted threshold slightly based on prev discussion
                    stop_ray = true;
                }

                if (log_this_pixel) {
                    printf("--- Mode=DT_TEXTURE\n");
                    printf("--- Collision Point: (%.3f, %.3f, %.3f)\n", col_point.x, col_point.y, col_point.z);
                    printf("--- phi=%.4f, r=%.4f\n", phi, r);
                    printf("--- UV=(%.4f, %.4f)\n", u, v);
                    printf("--- Looked up color (linear): (%.3f, %.3f, %.3f)\n", disk_color.r, disk_color.g, disk_color.b);
                    printf("--- Color norm^2=%.4f\n", color_norm_sq);
                    printf("--- Resulting disk_alpha=%.4f\n", disk_alpha);
                }
            } else {
                disk_alpha = 0.0; // No texture loaded
                if (log_this_pixel) printf("--- Disk texture not loaded!\n");
            }
            break;
        case DT_BLACKBODY: {
            double log_temp = bb_log_temperature(col_point_sqr, DEFAULT_LOG_T0_ISCO);
            double temp = exp(log_temp);
            double R = sqrt(col_point_sqr);

            if (cfg->redshift != 1.0 && R > sqrt(MIN_GRAV_REDSHIFT_R_SQR)) { // Apply redshift if enabled and outside horizon slightly
                 // Formula from Python code for velocity factor
                double speed_factor = BBODY_SPEED_FACTOR * pow(fmax(MIN_VEL_R_SQR, R - sqrt(SCHWARZSCHILD_RADIUS_SQR)), -0.5);
                Vec3d disk_vel_dir = vec3d_cross(VEC3D_UP, vec3d_normalize(col_point));
                Vec3d disk_vel = vec3d_mul_scalar(disk_vel_dir, speed_factor);
                double disk_vel_sqr = fmin(0.99, vec3d_norm_sqr(disk_vel)); // Clamp speed < c

                double gamma = 1.0 / sqrt(1.0 - disk_vel_sqr);
                double doppler_dot = vec3d_dot(disk_vel, vec3d_normalize(ray->vel));
                double opz_doppler = gamma * (1.0 - doppler_dot); // 1+z, NOTE: '-' sign used here based on standard physics

                double opz_grav = 1.0 / sqrt(fmax(EPSILON_LOOSE, 1.0 - sqrt(SCHWARZSCHILD_RADIUS_SQR)/R)); // 1+z grav sqrt(g_tt)

                double total_opz = opz_doppler * opz_grav * cfg->redshift;
                temp /= fmax(0.1, total_opz); // Correct temperature
            }

            // double intensity = bb_intensity(temp);
            ColorRGB bb_col = bb_color_from_temp(cfg, temp);

            // --- Apply multiplier ---
            if (cfg->disk_intensity_do) {
                // Multiply the color (which includes relative intensity) by the overall multiplier
                disk_color = color_mul_scalar(bb_col, cfg->disk_multiplier);
            } else {
                // If intensity factor is disabled, maybe just use the color normalized to 1?
                // Or still use bb_col as is? Let's assume we use bb_col as is for now.
                disk_color = bb_col;
            }

            // --- Alpha calculation ---
            double isco_taper = fmax(0.0, fmin(1.0, (col_point_sqr - cfg->disk_inner_sqr) * BBODY_ISCO_TAPER_FACTOR));
            double outer_taper = fmax(0.0, fmin(1.0, temp / BBODY_TEMP_TAPER_THRESHOLD));
            disk_alpha = isco_taper * outer_taper;

            // Optional: Clamp final color value before blend?
            // disk_color = color_clamp(disk_color, 0.0, BBODY_MAX_CLAMP_VALUE);

            if (disk_alpha >= 0.95) { // Stop if alpha is very high
                stop_ray = true;
            }
            break;
        }
        default: break;
    }

    // Blend disk color (cb=disk, ca=ray)
    if (log_this_pixel && disk_alpha > 1e-6) {
        printf("--- Blending Disk: Ray (%.3f,%.3f,%.3f a=%.3f) + Disk (%.3f,%.3f,%.3f a=%.3f)\n",
                ray->color.r, ray->color.g, ray->color.b, ray->alpha,
                disk_color.r, disk_color.g, disk_color.b, disk_alpha);
    }
    ray->color = blend_colors(disk_color, disk_alpha, ray->color, ray->alpha);
    ray->alpha = blend_alpha(disk_alpha, ray->alpha);

    if (log_this_pixel && disk_alpha > 1e-6) {
        printf("--- Result: (%.3f,%.3f,%.3f a=%.3f)\n", ray->color.r, ray->color.g, ray->color.b, ray->alpha);
    }

    return stop_ray;
}

// --- Helper: Handle Event Horizon Hit ---
static void handle_horizon_hit(RayState *ray, const Vec3d old_pos, double old_pos_sqr, const Config *cfg, bool log_this_pixel) {
    double alpha_before_hit = ray->alpha; // Store alpha before overwrite

    if (log_this_pixel) {
        printf("--- Iter %d: EVENT HORIZON HIT DETECTED!\n", ray->steps_taken);
        printf("--- Previous color was: (%.3f,%.3f,%.3f a=%.3f)\n", ray->color.r, ray->color.g, ray->color.b, alpha_before_hit);
    }

    // If ray was already significantly opaque (hit disk first), DO NOT overwrite color.
    if (alpha_before_hit > 0.1) { // Threshold can be adjusted
        if (log_this_pixel) {
            printf("--- Ray already had alpha %.3f (> 0.1), PRESERVING color, ignoring horizon overwrite.\n", alpha_before_hit);
        }
        // Ray stops, color remains as it was from the disk.
        ray->active = false;
        return; // Exit without blending horizon color
    }

    // Otherwise (ray was transparent), apply horizon color (black or grid)
    if (log_this_pixel) {
        printf("--- Ray was transparent (alpha=%.3f <= 0.1), applying horizon color.\n", alpha_before_hit);
    }

    ColorRGB horizon_color = COLOR_BLACK;
    if (cfg->horizon_grid) {
        // Interpolate collision point lambda
        // lambda = (sqrt(R_h^2) - sqrt(r_old^2)) / (sqrt(r_new^2) - sqrt(r_old^2)) approx
        // Or use linear interp on r^2: lambda = (R_h^2 - r_old^2) / (r_new^2 - r_old^2)
        // Let's stick to approximation using old_pos for angles as before for simplicity/consistency
        double r_old = sqrt(fmax(1e-9, old_pos_sqr)); // Avoid sqrt(0)
        double phi = atan2(old_pos.x, old_pos.z);
        double altitude = asin(fmax(-1.0, fmin(1.0, old_pos.y / r_old))); // Altitude angle

        bool phi_check = fmod(phi + GRID_ANGLE_OFFSET, GRID_HORIZON_PHI_STEP) < (GRID_HORIZON_PHI_STEP * 0.5);
        bool alt_check = fmod(altitude + M_PI/2.0 + GRID_ANGLE_OFFSET, GRID_HORIZON_ALT_STEP) < (GRID_HORIZON_ALT_STEP * 0.5);

        if (phi_check ^ alt_check) { // XOR
            horizon_color = (ColorRGB){1.0, 0.0, 0.0}; // Red grid lines
        }
    }

    double horizon_alpha = 1.0; // Opaque horizon

    // Blend horizon color (cb=horizon, ca=ray)
    ray->color = blend_colors(horizon_color, horizon_alpha, ray->color, alpha_before_hit);
    ray->alpha = blend_alpha(horizon_alpha, alpha_before_hit); // Will become 1.0

    ray->active = false; // Stop tracing this ray
}


// --- Helper: Apply Fog ---
static void apply_fog(RayState *ray, double current_pos_sqr, const Config *cfg) {
    if (!cfg->fog_do || (ray->steps_taken % cfg->fog_skip != 0)) {
        return; // Fog disabled or skip this step
    }

    // Only apply fog outside horizon
    if (current_pos_sqr > SCHWARZSCHILD_RADIUS_SQR) {
         double phsphtaper = fmax(0.0, fmin(1.0, FOG_TAPER_FACTOR * (current_pos_sqr - SCHWARZSCHILD_RADIUS_SQR)));
         double fog_int_base = cfg->fog_mult * cfg->fog_skip * cfg->step_size / fmax(1e-6, current_pos_sqr);
         double fog_alpha_step = fmax(0.0, fmin(1.0, fog_int_base)) * phsphtaper;
         ColorRGB fog_col = COLOR_WHITE; // Fog color is white

        // Blend fog (cb=fog, ca=ray)
        ray->color = blend_colors(fog_col, fog_alpha_step, ray->color, ray->alpha);
        ray->alpha = blend_alpha(fog_alpha_step, ray->alpha);
    }
}

// --- Helper: Get Background Sky Color ---
static ColorRGB get_background_color(const RayState *ray, const Config *cfg) {
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

    switch(cfg->sky_texture_mode) {
        case ST_TEXTURE:
            if (cfg->sky_texture) {
                bg_color = texture_lookup(cfg->sky_texture, sky_u, sky_v, cfg->srgb_in);
            } // else bg_color remains black
            break;
        case ST_FINAL: // Debug mode: color based on final direction component magnitudes
            bg_color = (ColorRGB){fabs(final_vel_norm.x), fabs(final_vel_norm.y), fabs(final_vel_norm.z)};
            break;
        case ST_NONE: // Fallthrough
        default:
            // bg_color is already black
            break;
    }
    // Apply sky brightness scaling AFTER lookup/calculation
    return color_mul_scalar(bg_color, cfg->sky_disk_ratio);
}


// --- The Refactored Trace a single ray for one pixel function ---
static ColorRGB trace_pixel(int px, int py, const Config *cfg) {
    bool log_this_pixel = (px == DEBUG_SINGLE_PIXEL_X && py == DEBUG_SINGLE_PIXEL_Y);
    // --- Debugging Output ---
    if (log_this_pixel) {
        printf("\n--- Logging for pixel (%d, %d)\n", px, py);
        printf("--- Disk inner radius: %f\n", cfg->disk_inner_radius);
        printf("--- Disk outer radius: %f\n", cfg->disk_outer_radius);
        printf("--- Redshift: %f\n", cfg->redshift);
        printf("--- Disk multiplier: %f\n", cfg->disk_multiplier);
    }

    // 1. Initialize Ray
    Vec3d initial_vel = calculate_initial_view_vector(px, py, cfg);
    RayState ray;
    initialize_ray_state(&ray, initial_vel, cfg);

    // 2. Integration Loop
    Vec3d old_pos;
    double old_pos_sqr;

    for (int it = 0; it < cfg->n_iterations; ++it) {
        if (!ray.active || ray.alpha >= MAX_RAY_ALPHA) break;

        old_pos = ray.pos;
        old_pos_sqr = vec3d_norm_sqr(old_pos);

        // --- Step ---
        perform_rk4_step(&ray, cfg->step_size, cfg->distort);
        double current_pos_sqr = vec3d_norm_sqr(ray.pos);

        // --- Check for Horizon Hit ---
        if (old_pos_sqr > SCHWARZSCHILD_RADIUS_SQR && current_pos_sqr <= SCHWARZSCHILD_RADIUS_SQR) {
            handle_horizon_hit(&ray, old_pos, old_pos_sqr, cfg, log_this_pixel);
            continue; // Skip disk/fog checks if horizon was hit this step
        }

        // --- Check for Disk Hit ---
        if (cfg->disk_texture_mode != DT_NONE && (old_pos.y * ray.pos.y < 0.0)) { // Crossed y=0 plane
            double delta_y = ray.pos.y - old_pos.y;
            if (fabs(delta_y) > 1e-9) { // Avoid division by zero if static on plane
                double t_cross = -old_pos.y / delta_y;
                if (t_cross >= -CROSSING_TOLERANCE && t_cross <= 1.0 + CROSSING_TOLERANCE) { // Intersection within step
                    t_cross = fmax(0.0, fmin(1.0, t_cross)); // Clamp t
                    Vec3d col_point = vec3d_add(old_pos, vec3d_mul_scalar(vec3d_sub(ray.pos, old_pos), t_cross));
                    double col_point_sqr = vec3d_norm_sqr(col_point);

                    if (col_point_sqr >= cfg->disk_inner_sqr && col_point_sqr <= cfg->disk_outer_sqr) { // Within disk radial bounds
                        bool stop_after_disk = handle_disk_hit(&ray, col_point, col_point_sqr, cfg, log_this_pixel);
                        if (stop_after_disk) {
                            ray.active = false;
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

    if (log_this_pixel) {
        printf("--- Loop finished. Accumulated color: (%.3f,%.3f,%.3f a=%.3f)\n",ray.color.r, ray.color.g, ray.color.b, ray.alpha);
    }

    // 3. Background / Sky Color Blending
    if (ray.alpha < MAX_RAY_ALPHA) { // If ray didn't hit something fully opaque
        ColorRGB bg_color = get_background_color(&ray, cfg);
        double sky_balpha = 1.0; // Sky is opaque background
        // Blend sky (cb=sky, ca=ray)
        ray.color = blend_colors(bg_color, sky_balpha, ray.color, ray.alpha);
        ray.alpha = blend_alpha(sky_balpha, ray.alpha); // Final alpha should be 1.0
    }

    if (log_this_pixel) {
        printf("--- Final pixel color: (%.3f,%.3f,%.3f)\n", ray.color.r, ray.color.g, ray.color.b);
    }

    return ray.color;
}


// --- Trace a range of pixels (for threading) ---
// (trace_pixel_range function remains largely the same, just calls the refactored trace_pixel)
static void* trace_pixel_range(void* thread_arg) {
    ThreadData *data = (ThreadData*)thread_arg;
    const Config *cfg = data->config;
    ImageF *image = data->image;
    int W = image->width;

    printf("Thread %d: Tracing pixels %d to %d\n", data->thread_id, data->start_pixel_index, data->end_pixel_index);
    clock_t start_time = clock(); // Simple timing per thread

    for (int idx = data->start_pixel_index; idx < data->end_pixel_index; ++idx) {
        int px = idx % W;
        int py = idx / W;
        image->pixels[idx] = trace_pixel(px, py, cfg); // Call the refactored function
    }

    clock_t end_time = clock();
    double time_spent = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    printf("Thread %d: Finished range in %.2f seconds.\n", data->thread_id, time_spent);

    return NULL;
}


// --- Main run_tracer Function ---
// Sets up and manages threads.
bool run_tracer(Config *config, ImageF *output_image) {
    if (!config || !output_image || !output_image->pixels) {
        fprintf(stderr, "Error: Invalid arguments passed to run_tracer.\n");
        return false;
    }

    int W = output_image->width;
    int H = output_image->height;
    int num_pixels = W * H;
    int n_threads = config->n_threads;

    if (n_threads <= 0) n_threads = 1; // Ensure at least one thread

    printf("Starting ray tracing with %d threads...\n", n_threads);
    printf("Total pixels: %d\n", num_pixels);

    // Allocate thread handles and data structures
    pthread_t *threads = (pthread_t*)malloc(n_threads * sizeof(pthread_t));
    ThreadData *thread_data = (ThreadData*)malloc(n_threads * sizeof(ThreadData));

    if (!threads || !thread_data) {
        fprintf(stderr, "Error: Failed to allocate memory for thread management.\n");
        free(threads);
        free(thread_data);
        return false;
    }

    // Divide work among threads
    int pixels_per_thread = num_pixels / n_threads;
    int remaining_pixels = num_pixels % n_threads;
    int current_pixel_index = 0;

    clock_t total_start_time = clock();

    for (int i = 0; i < n_threads; ++i) {
        thread_data[i].thread_id = i;
        thread_data[i].config = config;
        thread_data[i].image = output_image;
        thread_data[i].start_pixel_index = current_pixel_index;

        int pixels_for_this_thread = pixels_per_thread + (i < remaining_pixels ? 1 : 0);
        thread_data[i].end_pixel_index = current_pixel_index + pixels_for_this_thread;

        current_pixel_index += pixels_for_this_thread;

        // Create thread
        int ret = pthread_create(&threads[i], NULL, trace_pixel_range, &thread_data[i]);
        if (ret != 0) {
            fprintf(stderr, "Error creating thread %d: %s\n", i, strerror(ret));
            // Should ideally try to join already created threads before failing
            n_threads = i; // Only wait for threads up to this point
            break; // Stop creating more threads
        }
    }

    // Wait for threads to complete
    printf("Waiting for %d threads to finish...\n", n_threads);
    int threads_failed = 0;
    for (int i = 0; i < n_threads; ++i) {
        int ret = pthread_join(threads[i], NULL);
        if (ret != 0) {
            fprintf(stderr, "Error joining thread %d: %s\n", i, strerror(ret));
            threads_failed++;
        }
    }

    clock_t total_end_time = clock();
    double total_time_spent = (double)(total_end_time - total_start_time) / CLOCKS_PER_SEC;

    // Cleanup thread resources
    free(threads);
    free(thread_data);

    if (threads_failed > 0) {
        fprintf(stderr, "Error: %d threads failed to join correctly.\n", threads_failed);
        // Decide if this constitutes overall failure
        // return false;
    }

    printf("All threads finished. Total ray tracing time: %.2f seconds.\n", total_time_spent);

    // Return true even if some threads had issues joining? Or false? Let's return true for now.
    return true;
}
