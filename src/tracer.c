#include "tracer.h"
#include "vector.h"
#include "color.h"
#include "config.h" // Already included via tracer.h
#include "image.h"  // Already included via tracer.h
#include "blackbody.h" // For disk blackbody calculations
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h> // For threading later
#include <string.h>
#include <time.h>    // For progress timing later


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


// --- Trace a single ray for one pixel ---
static ColorRGB trace_pixel(int px, int py, const Config *cfg) {
    int W = cfg->resolution[0];
    int H = cfg->resolution[1];

    bool log_this_pixel = (px == 1300 && py == 950);

    // --- Debugging Output ---
    if (log_this_pixel) {
        printf("\n--- Logging for pixel (%d, %d) of (%d, %d) ---\n", px, py, W, H);
        printf("--- Disk texture mode: %u\n", cfg->disk_texture_mode);
        printf("--- Disk inner radius: %f\n", cfg->disk_inner_radius);
        printf("--- Disk outer radius: %f\n", cfg->disk_outer_radius);
        printf("--- Redshift: %f\n", cfg->redshift);
        printf("--- Disk multiplier: %f\n", cfg->disk_multiplier);
    }

    // 1. Calculate initial view vector (like Python code)
    // Screen coordinates [-0.5, 0.5] for x, scaled for y
    double screen_x = ((double)px / W) - 0.5;
    double screen_y = (-(double)py / H + 0.5) * ((double)H / W); // Keep aspect ratio correct

    // Scale by FoV
    screen_x *= cfg->tan_fov;
    screen_y *= cfg->tan_fov;

    // Form vector in camera space (Z=1)
    Vec3d view_cam_space = {screen_x, screen_y, 1.0};

    // Rotate into world space using view matrix
    // view = np.einsum('jk,ik->ij',viewMatrix,view)
    // Equivalent to matrix * vector: view_world = viewMatrix * view_cam_space
    Vec3d view_world = {0,0,0};
    view_world = vec3d_add(view_world, vec3d_mul_scalar(cfg->view_matrix[0], view_cam_space.x)); // Left * x
    view_world = vec3d_add(view_world, vec3d_mul_scalar(cfg->view_matrix[1], view_cam_space.y)); // Up * y
    view_world = vec3d_add(view_world, vec3d_mul_scalar(cfg->view_matrix[2], view_cam_space.z)); // Front * z

    Vec3d initial_vel_norm = vec3d_normalize(view_world);

    // 2. Initialize Ray State
    RayState ray;
    ray.pos = cfg->camera_pos;
    ray.vel = initial_vel_norm; // Start with normalized velocity
    ray.initial_vel = initial_vel_norm; // Store for sky lookup
    ray.color = COLOR_BLACK;
    ray.alpha = 0.0;
    ray.active = true;
    ray.steps_taken = 0;

    // Calculate h^2 (squared specific angular momentum)
    Vec3d initial_momentum = vec3d_cross(ray.pos, ray.vel);
    ray.h2 = vec3d_norm_sqr(initial_momentum);

    // 3. Integration Loop
    Vec3d old_pos = ray.pos;
    double old_pos_sqr = vec3d_norm_sqr(old_pos);

    for (int it = 0; it < cfg->n_iterations; ++it) {
        if (!ray.active || ray.alpha >= 0.9999) break; // Stop if ray hit something opaque or finished

        old_pos = ray.pos;
        old_pos_sqr = vec3d_norm_sqr(old_pos);

        // Perform integration step (RK4 or Euler)
        perform_rk4_step(&ray, cfg->step_size, cfg->distort);

        double current_pos_sqr = vec3d_norm_sqr(ray.pos);

        // 4. Collision Checks & Blending

        // --- Event Horizon Check ---
        // Check if crossed from r > 1 to r <= 1
        if (old_pos_sqr > 1.0 && current_pos_sqr <= 1.0) {

            if (log_this_pixel) {
                printf("--- Iter %d: EVENT HORIZON HIT! Overwriting color.\n", it);
                printf("--- Previous color was: (%.3f,%.3f,%.3f a=%.3f)\n", ray.color.r, ray.color.g, ray.color.b, ray.alpha);
            }

            // Simple: just stop and set color to black or grid
            ColorRGB horizon_color = COLOR_BLACK;
            if (cfg->horizon_grid) {
                // Interpolate collision point for grid calculation? More complex.
                // Simple approximation: use old_pos for angles
                // Need more robust interpolation later if grid is important
                double phi = atan2(old_pos.x, old_pos.z);
                double theta = atan2(old_pos.y, sqrt(old_pos.x*old_pos.x + old_pos.z*old_pos.z)); // Approx latitude
                // Python: np.logical_xor(np.mod(phi,1.04719) < 0.52359,np.mod(theta,1.04719) < 0.52359)
                bool phi_check = fmod(phi + 100*M_PI, 1.04719) < 0.52359; // Add large multiple of PI to handle negative phi
                bool theta_check = fmod(theta + 100*M_PI, 1.04719) < 0.52359;
                if (phi_check ^ theta_check) { // XOR
                    horizon_color = (ColorRGB){1.0, 0.0, 0.0}; // Red grid lines
                } else {
                    // Keep horizon_color black if not on grid line
                }

            }
            double horizon_alpha = 1.0; // Opaque horizon
            ray.color = blend_colors(ray.color, ray.alpha, horizon_color, horizon_alpha);
            ray.alpha = blend_alpha(ray.alpha, horizon_alpha);
            ray.active = false; // Stop tracing this ray
            continue; // Skip other checks for this step
        }

        // --- Accretion Disk Check ---
        // Check if crossed the y=0 plane
        if (cfg->disk_texture_mode != DT_NONE && (old_pos.y * ray.pos.y < 0.0)) {
            // Check if within radial bounds at the crossing point
            // Interpolate crossing point: P_cross = old_pos + t * (ray.pos - old_pos)
            // where P_cross.y = 0 => old_pos.y + t * (ray.pos.y - old_pos.y) = 0
            // t = -old_pos.y / (ray.pos.y - old_pos.y)
            double t_cross = -old_pos.y / (ray.pos.y - old_pos.y);
            // Ensure t is within [0, 1] to be between old and current pos
            if (t_cross >= -1e-6 && t_cross <= 1.0 + 1e-6) { // Allow small tolerance
                t_cross = fmax(0.0, fmin(1.0, t_cross)); // Clamp t
                Vec3d col_point = vec3d_add(old_pos, vec3d_mul_scalar(vec3d_sub(ray.pos, old_pos), t_cross));
                double col_point_sqr = vec3d_norm_sqr(col_point);

                if (col_point_sqr >= cfg->disk_inner_sqr && col_point_sqr <= cfg->disk_outer_sqr) {
                    // Collision within disk bounds!
                    ColorRGB disk_color = COLOR_BLACK;
                    double disk_alpha = 0.0; // Default to transparent

                    // Determine color/alpha based on disk mode
                    switch(cfg->disk_texture_mode) {
                        case DT_GRID: {
                             double phi = atan2(col_point.x, col_point.z);
                             // Python: np.mod(phi,0.52359) < 0.261799
                             bool phi_check = fmod(phi + 100*M_PI, 0.52359) < 0.261799;
                             if (phi_check) disk_color = (ColorRGB){1.0, 1.0, 0.0}; // Yellow
                             else disk_color = (ColorRGB){0.0, 0.0, 1.0};           // Blue
                             disk_alpha = 1.0;
                            break;
                        }
                        case DT_SOLID:
                            disk_color = (ColorRGB){1.0, 1.0, 0.98};
                            disk_alpha = 1.0;
                            break;
                        case DT_TEXTURE:
                            if (cfg->disk_texture) {
                                double phi = atan2(col_point.x, col_point.z);
                                double r = sqrt(col_point_sqr);
                                double u = fmod(phi + 2.0 * M_PI, 2.0 * M_PI) / (2.0 * M_PI); // Normalize phi to [0, 1]
                                double v = (r - cfg->disk_inner_radius) / (cfg->disk_outer_radius - cfg->disk_inner_radius); // Normalize radius

                                // Perform lookup FIRST
                                ColorRGB looked_up_color = texture_lookup(cfg->disk_texture, u, v, cfg->srgb_in);
                                disk_color = looked_up_color;

                                // Python alpha was: diskmask * np.clip(sqrnorm(diskcolor)/3.0,0.0,1.0)
                                double color_norm_sq = vec3d_norm_sqr(*(Vec3d*)&disk_color); // Treat ColorRGB as Vec3d for norm_sqr
                                disk_alpha = fmax(0.0, fmin(1.0, color_norm_sq / 3.0));


                                // --- Add Detailed Logging (for one specific pixel) ---
                                // Example: Log for pixel near center-left, which should be bright
                                // printf("Pixel (%d, %d) Disk Hit (Texture Mode):\n", px, py);

                                if (log_this_pixel) {
                                    printf("--- Mode=DT_TEXTURE\n");
                                    printf("--- Collision Point: (%.3f, %.3f, %.3f)\n", col_point.x, col_point.y, col_point.z);
                                    printf("--- phi=%.4f, r=%.4f\n", phi, r);
                                    printf("--- UV=(%.4f, %.4f)\n", u, v);
                                    printf("--- Looked up color (linear): (%.3f, %.3f, %.3f)\n", disk_color.r, disk_color.g, disk_color.b);
                                    printf("--- Color norm^2=%.4f\n", color_norm_sq);
                                    printf("--- Resulting disk_alpha=%.4f\n", disk_alpha);
                               }
                               // --- End Logging ---

                            } else { // No texture loaded
                                disk_alpha = 0.0;
                                if (log_this_pixel) { // Log even if texture missing
                                    printf("--- Iter %d: Disk texture not loaded!\n", it);
                                }
                            }
                            break;
                        case DT_BLACKBODY: {
                            // Temperature calculation
                            double log_temp = bb_log_temperature(col_point_sqr, 9.2103); // 9.2103 = log(10000) approx? T_isco=10000K?
                            double temp = exp(log_temp);

                            // Redshift calculation (complex!)
                            if (cfg->redshift != 1.0) { // Simplified check if redshift effect is enabled via factor != 1
                                double R = sqrt(col_point_sqr);
                                // Approx Schwarzschild orbital velocity for disk at R (v/c = 1/sqrt(2*(R-1))?)
                                // v/c = sqrt(M / (2*r * (1-M/r))) -> sqrt(1 / (2R(1-1/R))) = 1/sqrt(2*(R-1)) ?? No..
                                // v/c = sqrt(GM / r) / c = sqrt(M/r) in G=c=1 units -> sqrt(1/R) ??
                                // Schwarzschild circular orbit v/c = sqrt(M / (r - 2M)) => sqrt(1 / (R-2))?? No...
                                // v/c = sqrt(M/r) is Newtonian. GR: v_phi = sqrt(M/r^2 / (d phi / dt)) ... Let's use python v
                                // disc_velocity = 0.70710678 * \
                                //      np.power((np.sqrt(colpointsqr)-1.).clip(0.1),-.5)[:,np.newaxis] * \
                                //      np.cross(UPFIELD, normalize(colpoint))
                                 if (R > 1.001) { // Avoid singularity slightly outside horizon
                                    double speed_factor = 0.70710678 * pow(fmax(0.1, R - 1.0), -0.5);
                                    Vec3d disk_vel_dir = vec3d_cross((Vec3d){0,1,0}, vec3d_normalize(col_point));
                                    Vec3d disk_vel = vec3d_mul_scalar(disk_vel_dir, speed_factor);
                                    double disk_vel_sqr = vec3d_norm_sqr(disk_vel);
                                    disk_vel_sqr = fmin(0.99, disk_vel_sqr); // Clamp speed < c

                                    double gamma = 1.0 / sqrt(1.0 - disk_vel_sqr);
                                    double doppler_dot = vec3d_dot(disk_vel, vec3d_normalize(ray.vel));
                                    double opz_doppler = gamma * (1.0 + doppler_dot); // 1+z for doppler
                                    double opz_grav = 1.0 / sqrt(fmax(0.001, 1.0 - 1.0/R)); // 1+z for grav redshift sqrt(g_tt)
                                    // Apply redshift factor from config as well?
                                    double total_opz = opz_doppler * opz_grav * cfg->redshift;
                                    temp /= fmax(0.1, total_opz); // Correct temperature
                                 }
                            }

                            double intensity = bb_intensity(temp);
                            if (cfg->disk_intensity_do) {
                                disk_color = color_mul_scalar(bb_color_from_temp(temp), cfg->disk_multiplier * intensity);
                            } else {
                                disk_color = bb_color_from_temp(temp);
                            }

                            // Alpha based on temp taper (like python)
                            double isco_taper = fmax(0.0, fmin(1.0, (col_point_sqr - cfg->disk_inner_sqr) * 0.3));
                            double outer_taper = fmax(0.0, fmin(1.0, temp / 1000.0));
                            disk_alpha = isco_taper * outer_taper;
                            disk_color = color_clamp(disk_color, 0.0, 100.0); // Clamp color intensity before blend?
                            break;
                        }
                        case DT_NONE: default: break; // Should not happen due to outer check
                    }

                    // Blend disk color
                    if (log_this_pixel) {
                        printf("--- Blending: Old (%.3f,%.3f,%.3f a=%.3f) + Disk (%.3f,%.3f,%.3f a=%.3f)\n",
                                ray.color.r, ray.color.g, ray.color.b, ray.alpha,
                                disk_color.r, disk_color.g, disk_color.b, disk_alpha);
                    }

                    ray.color = blend_colors(ray.color, ray.alpha, disk_color, disk_alpha);
                    ray.alpha = blend_alpha(ray.alpha, disk_alpha);
                    // Should the ray stop? Assume disk is semi-transparent based on alpha.
                    // If disk_alpha == 1.0, we could set ray.active = false;

                    if (log_this_pixel) {
                        printf("--- Result: (%.3f,%.3f,%.3f a=%.3f)\n", ray.color.r, ray.color.g, ray.color.b, ray.alpha);
                    }

                    if (ray.alpha > 0.85) { // Threshold adjustable
                        if (log_this_pixel) {
                           printf("--- Iter %d: Ray considered stopped by opaque disk hit (alpha=%.3f).\n", it, ray.alpha);
                        }
                        ray.active = false;
                    }
                } // end if within bounds
            } // end if t_cross valid
        } // end if plane crossed

        // --- Fog ---
        if (cfg->fog_do && (it % cfg->fog_skip == 0)) {
            // phsphtaper = np.clip(0.8*(pointsqr - 1.0),0.,1.0)
            // fogint = np.clip(FOGMULT * FOGSKIP * STEP / pointsqr,0.0,1.0) * phsphtaper
            if (current_pos_sqr > 1.0) { // Only apply fog outside horizon
                 double phsphtaper = fmax(0.0, fmin(1.0, 0.8 * (current_pos_sqr - 1.0)));
                 double fog_int_base = cfg->fog_mult * cfg->fog_skip * cfg->step_size / fmax(1e-6, current_pos_sqr);
                 double fog_int = fmax(0.0, fmin(1.0, fog_int_base)) * phsphtaper;
                 ColorRGB fog_col = COLOR_WHITE; // Fog color is white

                 // Fog seems to be additive in the Python blend logic? Let's use standard blend.
                 ray.color = blend_colors(ray.color, ray.alpha, fog_col, fog_int);
                 ray.alpha = blend_alpha(ray.alpha, fog_int);
            }
        }

        // Check if ray has escaped to infinity (optional, maybe just let it hit max iterations)
        // if (current_pos_sqr > some_large_radius_sqr) { ray.active = false; }

        if (log_this_pixel && !ray.active) {
            printf("--- Iter %d: Ray deactivated.\n", it);
         }

    } // End integration loop

    if (log_this_pixel) {
        printf("--- Loop finished. Accumulated color: (%.3f,%.3f,%.3f a=%.3f)\n",ray.color.r, ray.color.g, ray.color.b, ray.alpha);
    }

    // 5. Background / Sky Color
    if (ray.alpha < 0.9999) { // If ray didn't hit something opaque
        ColorRGB bg_color = COLOR_BLACK;
        double bg_alpha = 1.0 - ray.alpha; // Remaining alpha for background

        // Use final velocity direction for sky lookup (like Python)
        Vec3d final_vel_norm = vec3d_normalize(ray.vel);

        // Calculate UV based on final velocity direction
        double vphi = atan2(final_vel_norm.x, final_vel_norm.z);
        double vtheta = atan2(final_vel_norm.y, sqrt(final_vel_norm.x*final_vel_norm.x + final_vel_norm.z*final_vel_norm.z)); // atan2(y, sqrt(x^2+z^2))

        // Map angles to UV [0, 1] (matching Python)
        // vuv[:,0] = np.mod(vphi+4.5,2*np.pi)/(2*np.pi) ?? +4.5 is strange offset. Use standard spherical?
        // vuv[:,1] = (vtheta+np.pi/2)/(np.pi)
        double sky_u = fmod(vphi + 2.0*M_PI, 2.0 * M_PI) / (2.0 * M_PI); // Standard longitude [0, 1]
        double sky_v = (vtheta + 0.5 * M_PI) / M_PI;                    // Standard latitude [-pi/2, pi/2] -> [0, 1]


        switch(cfg->sky_texture_mode) {
            case ST_TEXTURE:
                if (cfg->sky_texture) {
                    bg_color = texture_lookup(cfg->sky_texture, sky_u, sky_v, cfg->srgb_in);
                    // Python applied SKYDISK_RATIO here, let's do it too
                    bg_color = color_mul_scalar(bg_color, cfg->sky_disk_ratio);
                }
                break;
            case ST_FINAL: // Debug mode: color based on final direction
                 bg_color = (ColorRGB){fabs(final_vel_norm.x), fabs(final_vel_norm.y), fabs(final_vel_norm.z)};
                 bg_color = color_mul_scalar(bg_color, cfg->sky_disk_ratio);
                 break;
            case ST_NONE:
            default:
                 bg_color = COLOR_BLACK; // Already initialized
                 bg_color = color_mul_scalar(bg_color, cfg->sky_disk_ratio); // Applies gain=0 if ratio=0
                 break;
        }

        // Blend background behind accumulated ray color
        // Python: blendcolors(SKYDISK_RATIO*col_bg, ones ,object_colour,object_alpha)
        // Here: background=bg_color, bg_alpha=1.0, foreground=ray.color, fg_alpha=ray.alpha
        ray.color = blend_colors(bg_color, 1.0, ray.color, ray.alpha);
        // Final alpha is now effectively 1.0 after blending with opaque background
        ray.alpha = 1.0; // Or blend_alpha(1.0, ray.alpha) which should yield 1.0
    }

    if (log_this_pixel) {
        printf("--- Final pixel color: (%.3f,%.3f,%.3f)\n", ray.color.r, ray.color.g, ray.color.b);
    }

    // Return final pixel color (should be fully opaque now)
    return ray.color;
}


// --- Trace a range of pixels (for threading) ---
// This function will be called by each thread.
static void* trace_pixel_range(void* thread_arg) {
    ThreadData *data = (ThreadData*)thread_arg;
    const Config *cfg = data->config;
    ImageF *image = data->image;
    int W = image->width;
    int H = image->height;

    printf("Thread %d: Tracing pixels %d to %d\n", data->thread_id, data->start_pixel_index, data->end_pixel_index);
    clock_t start_time = clock(); // Simple timing per thread

    for (int idx = data->start_pixel_index; idx < data->end_pixel_index; ++idx) {
        int px = idx % W;
        int py = idx / W;

        // --- Trace Pixel ---
        ColorRGB final_color = trace_pixel(px, py, cfg);

        // --- Store Result ---
        image->pixels[idx] = final_color;

        // --- Progress Update (Optional, needs synchronization) ---
        // if ((idx - data->start_pixel_index) % 1000 == 0) {
        //     printf("Thread %d progress: %d / %d pixels\n",
        //            data->thread_id, idx - data->start_pixel_index,
        //            data->end_pixel_index - data->start_pixel_index);
        // }
    }

     clock_t end_time = clock();
     double time_spent = (double)(end_time - start_time) / CLOCKS_PER_SEC;
     printf("Thread %d: Finished range in %.2f seconds.\n", data->thread_id, time_spent);

    return NULL; // POSIX threads expect void* return
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
