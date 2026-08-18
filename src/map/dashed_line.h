#ifndef OPENRIDE_DASHED_LINE_H
#define OPENRIDE_DASHED_LINE_H

#include <math.h>
#include <stdint.h>

static inline uint64_t openride_dashed_line_endpoint_key(int tile_x,
                                                         int tile_y,
                                                         uint16_t local_x,
                                                         uint16_t local_y)
{
    const uint32_t global_x = (uint32_t)tile_x * UINT16_MAX + local_x;
    const uint32_t global_y = (uint32_t)tile_y * UINT16_MAX + local_y;
    return ((uint64_t)global_x << 32U) | global_y;
}

static inline float openride_dashed_line_normalize_phase(float phase,
                                                         float period)
{
    if (period <= 0.0f) return 0.0f;
    phase = fmodf(phase, period);
    return phase < 0.0f ? phase + period : phase;
}

static inline float openride_dashed_line_advance_phase(float phase,
                                                       float length,
                                                       float period)
{
    return openride_dashed_line_normalize_phase(phase + length, period);
}

#endif
