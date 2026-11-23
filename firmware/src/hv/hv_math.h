/* hv_math.h -- the few float functions the engine needs, without libm (the FM-1 links none).
 * Single precision only; every literal carries 'f' (the build has -Werror=double-promotion).
 * The same code runs on the host, so the host renders sound as the FM-1 does.
 * Copyright (c) 2026 Brian McCalla. GPL-3.0-only. */
#ifndef HV_MATH_H
#define HV_MATH_H
#include <stdint.h>

#define HV_PI 3.14159265f

static inline float hv_absf(float x) { return x < 0.f ? -x : x; }
static inline float hv_minf(float a, float b) { return a < b ? a : b; }
static inline float hv_maxf(float a, float b) { return a > b ? a : b; }
static inline float hv_clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
/* floor for |x| < 2^31 */
static inline float hv_floorf(float x) { int32_t i = (int32_t)x; return (float)(i - (x < (float)i)); }
static inline float hv_fracf(float x) { return x - hv_floorf(x); }

/* 2^x, error ~3e-6: integer part into the exponent, a Taylor polynomial around the middle */
static inline float hv_exp2f(float x) {
    x = hv_clampf(x, -60.f, 60.f);
    float fl = hv_floorf(x), f = (x - fl - 0.5f) * 0.6931472f;   /* e^f, |f| <= 0.35: Taylor to f^5 */
    float p = 1.4142136f * (1.f + f * (1.f + f * (0.5f + f * (0.1666667f + f * (0.0416667f + f * 0.0083333f)))));
    union { float f; int32_t i; } u = { p };
    u.i += (int32_t)fl << 23;
    return u.f;
}
static inline float hv_expf(float x) { return hv_exp2f(x * 1.442695f); }

/* sin of a phase in turns (0..1 = 0..2 pi), error ~0.1 % (a refined parabola) */
static inline float hv_sin_turn(float t) {
    t = t - hv_floorf(t + 0.5f);                 /* -0.5..0.5 */
    float y = 8.f * t - 16.f * t * hv_absf(t);   /* 4x(1-|x|) shape for x = 2t */
    return y + 0.225f * (y * hv_absf(y) - y);
}
static inline float hv_cos_turn(float t) { return hv_sin_turn(t + 0.25f); }

/* tan(pi x) for 0 <= x < 0.5: the filters' prewarp. good to ~0.5 % below 0.45 */
static inline float hv_tan_pi(float x) {        /* [5/4] Pade */
    float a = HV_PI * x, a2 = a * a;
    return a * (945.f - 105.f * a2 + a2 * a2) / (945.f - 420.f * a2 + 15.f * a2 * a2);
}

static inline float hv_sqrtf(float x) {          /* Newton from a bit-level guess: 3 steps, ~1e-6 */
    if (x <= 0.f) return 0.f;
    union { float f; int32_t i; } u = { x };
    u.i = 0x1fbd1df5 + (u.i >> 1);
    float y = u.f;
    y = 0.5f * (y + x / y); y = 0.5f * (y + x / y); y = 0.5f * (y + x / y);
    return y;
}

static inline float hv_tanh(float x) {           /* soft saturator, Pade, clamped */
    x = hv_clampf(x, -3.f, 3.f);
    return x * (27.f + x * x) / (27.f + 9.f * x * x);
}

#endif
