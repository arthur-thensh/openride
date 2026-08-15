#include <math.h>
#include <stdbool.h>

/*
 * ORMap screen rotation uses the same bearing angle thousands of times while
 * drawing one frame. Keep the most recent sine/cosine pair per render thread
 * so repeated geometry transforms do not call libm for every vertex.
 *
 * This file is intentionally compiled without the cos/sin remapping applied to
 * ormap_renderer.c, so the cache refresh itself always reaches the real math
 * implementation.
 */
typedef struct OpenRideORMapTrigCache {
    bool valid;
    double angle;
    double cosine;
    double sine;
} OpenRideORMapTrigCache;

static _Thread_local OpenRideORMapTrigCache g_ormap_trig_cache;

static void refresh_cache(double angle)
{
    if (g_ormap_trig_cache.valid && angle == g_ormap_trig_cache.angle) {
        return;
    }

    g_ormap_trig_cache.angle = angle;

    /*
     * Clang is used by both the macOS and Android OpenRide toolchains. Its
     * sincos builtin lets the backend evaluate both values together, avoiding
     * two independent libm argument-reduction paths whenever the camera
     * bearing changes. The common per-vertex path still only performs the
     * exact-angle cache check above.
     */
#if defined(__clang__) || defined(__GNUC__)
    __builtin_sincos(angle,
                     &g_ormap_trig_cache.sine,
                     &g_ormap_trig_cache.cosine);
#else
    g_ormap_trig_cache.cosine = cos(angle);
    g_ormap_trig_cache.sine = sin(angle);
#endif

    g_ormap_trig_cache.valid = true;
}

double openride_ormap_cached_cos(double angle)
{
    refresh_cache(angle);
    return g_ormap_trig_cache.cosine;
}

double openride_ormap_cached_sin(double angle)
{
    refresh_cache(angle);
    return g_ormap_trig_cache.sine;
}
