/* hv_engine.c -- fm1-hv voice engine. See hv_engine.h.
 * Copyright (c) 2026 Brian McCalla. GPL-3.0-only.
 *
 * Signal flow per voice (after the Virus classic):
 *   osc1 + osc2 (+sync, FM) + sub + noise + ring -> drive -> filter1 -> saturation -> filter2 -> amp
 * Routing: serial 4 / serial 6 / parallel / split. Then chorus and delay over the sum.
 * Value curves (cutoff, times, rates) are first guesses, to be tuned against real units.
 */
#include "hv_engine.h"
#include "hv_math.h"
#if !defined(HV_IN_FIRMWARE) || defined(DINGHY_HOST)
#include <string.h>          /* (the firmware has its own memset / memcpy, libc.c) */
#endif
#ifndef HV_POOL
#define HV_POOL               /* the device build puts big buffers in the pool section */
#endif

static float hv_fs = 44100.f, hv_inv_fs = 1.f / 44100.f;
static uint8_t P[256];

/* ---------- small helpers ---------- */
static inline float u7(int i) { return P[i] * (1.f / 127.f); }          /* 0..1 */
static inline float s7(int i) { return (P[i] - 64) * (1.f / 64.f); }    /* -1..+1 */
static inline float note_hz(float n) { return 440.f * hv_exp2f((n - 69.f) * (1.f / 12.f)); }
static uint32_t rng = 22222;
static inline float white(void) { rng = rng * 1664525u + 1013904223u; return (int32_t)rng * (1.f / 2147483648.f); }

/* time constants: 0..127 -> seconds, exponential, ~1 ms .. ~10 s (guess) */
static inline float env_time(int v) { return 0.0008f * hv_expf(v * (9.4f / 127.f)); }
static inline float lfo_hz(int v) { return 0.02f * hv_expf(v * (8.3f / 127.f)); }      /* ~0.02..80 Hz */
/* cutoff "units" 0..127 (may go outside) -> Hz, ~20 Hz .. 20 kHz */
static inline float cut_hz(float u) { return 20.f * hv_exp2f(u * (9.97f / 127.f)); }

/* polyBLEP residual */
static inline float blep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
    if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}

/* ---------- spectral "wave select" placeholders ----------
 * Wave 0 = sine, 1 = triangle, 2..63 = generated spectra. The real tables are imported
 * by the user (see docs/WAVETABLES.md); these only keep patches that use them sounding. */
#define WT_LEN 256
static int16_t wt[64][WT_LEN] HV_POOL;
static float wtmp[WT_LEN];
static void build_waves(void) {
    for (int w = 0; w < 64; w++) {
        for (int i = 0; i < WT_LEN; i++) {
            float ph = (float)i / WT_LEN, s = 0.f;
            if (w == 0) s = hv_sin_turn(ph);
            else if (w == 1) { float x = (float)i / WT_LEN; s = x < .5f ? 4.f * x - 1.f : 3.f - 4.f * x; }
            else {                                   /* a formant-ish peak that moves with w */
                float centre = 1.f + (w - 2) * 0.5f, width = 1.5f + (w & 7) * 0.4f;
                for (int h = 1; h <= 24; h++) {
                    float d = (h - centre) / width, a = hv_expf(-d * d) + 0.15f / h;
                    s += a * hv_sin_turn(ph * h) / hv_sqrtf((float)h);
                }
            }
            wtmp[i] = s;
        }
        float m = 1e-6f;
        for (int i = 0; i < WT_LEN; i++) m = hv_maxf(m, hv_absf(wtmp[i]));
        for (int i = 0; i < WT_LEN; i++) wt[w][i] = (int16_t)(wtmp[i] / m * 32767.f);
    }
}
static inline float wt_read(int w, float ph) {
    float x = ph * WT_LEN; int i = (int)x; float f = x - i;
    float a = wt[w][i & (WT_LEN - 1)], b = wt[w][(i + 1) & (WT_LEN - 1)];
    return (a + (b - a) * f) * (1.f / 32767.f);
}

/* ---------- envelope (ADSR with Virus-style sustain slope) ---------- */
typedef struct { int st; float lvl; } env_t;            /* st 0 off,1 A,2 D,3 S,4 R */
static void env_run(env_t *e, int base, float dt) {
    float a = env_time(P[base]), d = env_time(P[base + 1]), s = u7(base + 2);
    int stv = P[base + 3]; float r = env_time(P[base + 4]);
    switch (e->st) {
    case 1: e->lvl += dt / a; if (e->lvl >= 1.f) { e->lvl = 1.f; e->st = 2; } break;
    case 2: e->lvl += (s - e->lvl) * (1.f - hv_expf(-dt * 4.f / d));
            if (hv_absf(e->lvl - s) < 0.001f) e->st = 3;
            break;
    case 3: if (stv != 64) {                              /* <64 falls, >64 rises */
                float rate = (stv - 64) / 64.f; rate = rate * rate * rate * 4.f;
                e->lvl = hv_clampf(e->lvl + rate * dt, 0.f, 1.f);
            } break;
    case 4: e->lvl *= hv_expf(-dt * 4.f / r); if (e->lvl < 0.0002f) { e->lvl = 0.f; e->st = 0; } break;
    }
}

/* ---------- LFO ---------- */
typedef struct { float ph, sh, last; } lfo_t;
static float lfo_run(lfo_t *l, int rate_i, int shape_i, float dt) {
    float hz = lfo_hz(P[rate_i]);
    l->ph += hz * dt;
    if (l->ph >= 1.f) { l->ph -= hv_floorf(l->ph); l->last = l->sh; l->sh = white(); }
    float p = l->ph;
    switch (P[shape_i]) {
    case 0: return hv_sin_turn(p);
    case 1: return p < .5f ? 4.f * p - 1.f : 3.f - 4.f * p;
    case 2: return 2.f * p - 1.f;
    case 3: return p < .5f ? 1.f : -1.f;
    case 4: return l->sh;
    case 5: return l->last + (l->sh - l->last) * p;
    default: return wt_read((P[shape_i] - 4) & 63, p);
    }
}

/* ---------- filters ---------- */
typedef struct { float ic1, ic2; } svf_t;
static inline float svf_tick(svf_t *f, float x, float a1, float a2, float a3, float k, int mode) {
    float v3 = x - f->ic2, v1 = a1 * f->ic1 + a2 * v3, v2 = f->ic2 + a2 * f->ic1 + a3 * v3;
    f->ic1 = 2.f * v1 - f->ic1; f->ic2 = 2.f * v2 - f->ic2;
    switch (mode & 3) {
    case 0: return v2;
    case 1: return x - k * v1 - v2;
    case 2: return v1 * k;               /* band, gain-normalised */
    default: return x - k * v1;          /* notch */
    }
}
typedef struct { float s[4]; } ladder_t;
/* Zavalishin TPT ladder; out = the pole count's stage; tanh at the input */
static inline float ladder_tick(ladder_t *l, float x, float G, float k, int poles) {
    float G2 = G * G, S = (G2 * G * l->s[0] + G2 * l->s[1] + G * l->s[2] + l->s[3]) * (1.f - G);
    float u = hv_tanh((x - k * S) / (1.f + k * G2 * G2)) * (1.f + k * 0.25f);
    float y = u, out = 0.f;
    for (int i = 0; i < 4; i++) {
        float v = (y - l->s[i]) * G;
        y = v + l->s[i]; l->s[i] = y + v;
        if (i + 1 == poles) out = y;
    }
    return out;
}

static float saturate(float x, int curve) {
    switch (curve) {
    case 0: return x;
    case 1: return x - x * x * x * (1.f / 9.f) * (hv_absf(x) < 1.5f);   /* light */
    case 2: return hv_tanh(x);
    case 3: return hv_tanh(1.6f * x) * 0.8f;
    case 4: return hv_clampf(x, -1.f, 1.f);
    case 5: return hv_sin_turn(hv_clampf(x, -3.f, 3.f) * 0.2069f);                  /* "digital" fold */
    case 6: return x / (1.f + hv_absf(x)) * 1.6f;                       /* shaper */
    case 7: return hv_absf(x) * 1.4f - 0.5f;                            /* rectifier */
    case 8: { float q = 16.f; return hv_floorf(x * q) / q; }             /* bit reducer */
    default: return hv_tanh(x);
    }
}

/* ---------- voice ---------- */
typedef struct {
    int on, note, gate; float vel, age;
    float pitch, target;                     /* MIDI note, glides */
    float ph1, ph2, ph3, phs;                /* osc phases, sub */
    float detune, pan;                       /* unison offsets */
    env_t fe, ae;
    svf_t f1, f1b, f2; ladder_t lad;
    lfo_t l1, l2;
    float lfo1, lfo2;
} voice_t;
static voice_t V[HV_NVOICE];
static int age_counter;

/* per-block derived voice settings, then the sample loop */
static void voice_render(voice_t *v, float *L, float *R, int n) {
    float dt = n * hv_inv_fs;
    /* glide */
    float porta = P[PA_PORTA];
    if (porta > 0) v->pitch += (v->target - v->pitch) * (1.f - hv_expf(-dt / (0.002f * hv_expf(porta * 0.055f))));
    else v->pitch = v->target;

    env_run(&v->fe, PA_FE_A, dt);
    env_run(&v->ae, PA_AE_A, dt);
    if (!v->ae.st) { v->on = 0; return; }
    v->lfo1 = lfo_run(&v->l1, PA_L1_RATE, PA_L1_SHAPE, dt);
    v->lfo2 = lfo_run(&v->l2, PA_L2_RATE, PA_L2_SHAPE, dt);
    float fenv = v->fe.lvl, l1 = v->lfo1, l2 = v->lfo2;

    /* pitches */
    float base = v->pitch + (P[PA_TRANSPOSE] - 64) + v->detune;
    float n1 = base + (P[PA_O1_SEMI] - 64) + s7(PA_L1_O1) * l1 * 12.f;
    float det = u7(PA_O2_DETUNE); det = det * det * 0.5f;            /* up to +50 cents */
    float n2 = base + (P[PA_O2_SEMI] - 64) + det + s7(PA_O2_FENV) * fenv * 24.f + s7(PA_L1_O2) * l1 * 12.f;
    float inc1 = note_hz(n1) * hv_inv_fs, inc2 = note_hz(n2) * hv_inv_fs;
    inc1 = hv_clampf(inc1, 0.f, 0.45f); inc2 = hv_clampf(inc2, 0.f, 0.45f);
    float fm = u7(PA_O2_FM) + s7(PA_FM_FENV) * fenv + s7(PA_L2_FM) * l2 * 0.5f;
    fm = hv_clampf(fm, 0.f, 1.f); fm = fm * fm * 4.f;
    int sync = P[PA_O2_SYNC];

    float shp1 = u7(PA_O1_SHAPE) + s7(PA_L2_SHP) * l2 * 0.5f, shp2 = u7(PA_O2_SHAPE);
    shp1 = hv_clampf(shp1, 0.f, 1.f);
    float pw1 = 0.5f + 0.49f * hv_clampf(u7(PA_O1_PW) + s7(PA_L1_PW) * l1, 0.f, 1.f);
    float pw2 = 0.5f + 0.49f * u7(PA_O2_PW);
    int w1 = P[PA_O1_WAVE] & 63, w2 = P[PA_O2_WAVE] & 63;
    float bal = (s7(PA_OSC_BAL) + 1.f) * 0.5f, g1 = 1.f - bal, g2 = bal;
    float subv = u7(PA_SUB_VOL), noisev = u7(PA_NOISE_VOL), ringv = u7(PA_RING_VOL);
    int subtri = P[PA_SUB_SHAPE];
    float o3v = u7(HV_B(PB_O3_VOL)); int o3m = P[HV_B(PB_O3_MODE)];
    float inc3 = hv_clampf(note_hz(base + (P[HV_B(PB_O3_SEMI)] - 64) + u7(HV_B(PB_O3_DET)) * 0.3f) * hv_inv_fs, 0.f, .45f);
    float ov = P[PA_OSC_VOL] <= 64 ? P[PA_OSC_VOL] / 64.f : 1.f + (P[PA_OSC_VOL] - 64) / 21.f;  /* >64 drives */

    /* filters */
    float key = v->pitch - 60.f;
    float pol1 = P[HV_B(PB_F1_POL)] ? 1.f : -1.f, pol2 = P[HV_B(PB_F2_POL)] ? 1.f : -1.f;
    float vel_env = 1.f;
    float u1 = P[PA_CUT1] + pol1 * u7(PA_F1_ENV) * fenv * 127.f * vel_env + s7(PA_F1_KEYF) * key * 2.f
             + s7(PA_L2_CUT1) * l2 * 64.f;
    float c2 = P[HV_B(PB_F2_LINK)] ? P[PA_CUT1] + (P[PA_CUT2] - 64) : P[PA_CUT2];
    float u2 = c2 + pol2 * u7(PA_F2_ENV) * fenv * 127.f + s7(PA_F2_KEYF) * key * 2.f + s7(PA_L2_CUT2) * l2 * 64.f;
    float hz1 = hv_clampf(cut_hz(u1), 15.f, hv_fs * 0.45f), hz2 = hv_clampf(cut_hz(u2), 15.f, hv_fs * 0.45f);
    float r1 = hv_clampf(u7(PA_RES1) + s7(PA_L1_RES) * l1, 0.f, 1.f), r2 = u7(PA_RES2);
    int m1 = P[PA_F1_MODE] & 7, m2 = P[PA_F2_MODE] & 3, route = P[PA_FROUTE] & 3, sat = P[PA_SAT];
    float gg1 = hv_tan_pi(hz1 * hv_inv_fs), gg2 = hv_tan_pi(hz2 * hv_inv_fs);
    float k1 = 2.f - 1.98f * r1, k2 = 2.f - 1.98f * r2;
    float a11 = 1.f / (1.f + gg1 * (gg1 + k1)), a21 = gg1 * a11, a31 = gg1 * a21;
    float a12 = 1.f / (1.f + gg2 * (gg2 + k2)), a22 = gg2 * a12, a32 = gg2 * a22;
    float Gl = gg1 / (1.f + gg1), kl = r1 * 4.2f;
    float fb = (s7(PA_FBAL) + 1.f) * 0.5f;

    /* amp */
    float amp = v->ae.lvl * (1.f + s7(PA_L1_GAIN) * l1 * 0.5f);
    float vs = s7(HV_B(PB_AMP_VEL));
    amp *= 1.f - hv_absf(vs) + hv_absf(vs) * v->vel;
    amp *= u7(PA_VOL) * 0.6f;
    float pan = hv_clampf(v->pan + s7(PA_PAN) + s7(PA_L2_PAN) * l2, -1.f, 1.f);
    float gl = hv_sqrtf(0.5f * (1.f - pan)), gr = hv_sqrtf(0.5f * (1.f + pan));

    for (int i = 0; i < n; i++) {
        /* osc1 */
        float p1 = v->ph1, s1;
        float saw1 = 2.f * p1 - 1.f - blep(p1, inc1);
        if (shp1 < .5f) s1 = wt_read(w1, p1) * (1.f - shp1 * 2.f) + saw1 * shp1 * 2.f;
        else {
            float q = p1 + (1.f - pw1); q -= (int)q;
            float pul = saw1 - (2.f * q - 1.f - blep(q, inc1));
            s1 = saw1 * (2.f - shp1 * 2.f) + pul * (shp1 * 2.f - 1.f);
        }
        /* osc2, FM from osc1 */
        float i2 = inc2 * (1.f + fm * s1);
        float p2 = v->ph2, s2;
        float saw2 = 2.f * p2 - 1.f - blep(p2, inc2);
        if (shp2 < .5f) s2 = wt_read(w2, p2) * (1.f - shp2 * 2.f) + saw2 * shp2 * 2.f;
        else {
            float q = p2 + (1.f - pw2); q -= (int)q;
            float pul = saw2 - (2.f * q - 1.f - blep(q, inc2));
            s2 = saw2 * (2.f - shp2 * 2.f) + pul * (shp2 * 2.f - 1.f);
        }
        /* sub: an octave under osc1 */
        float sub = subtri ? (v->phs < .5f ? 4.f * v->phs - 1.f : 3.f - 4.f * v->phs)
                           : (v->phs < .5f ? 1.f : -1.f);
        float o3 = 0.f;
        if (o3m && o3v > 0.f) { o3 = 2.f * v->ph3 - 1.f - blep(v->ph3, inc3); v->ph3 += inc3; if (v->ph3 >= 1.f) v->ph3 -= 1.f; }

        /* advance phases */
        v->ph1 += inc1;
        int wrap1 = v->ph1 >= 1.f;
        if (wrap1) { v->ph1 -= 1.f; }
        v->phs += inc1 * 0.5f; if (v->phs >= 1.f) v->phs -= 1.f;
        v->ph2 += i2; if (v->ph2 >= 1.f) v->ph2 -= 1.f; if (v->ph2 < 0.f) v->ph2 += 1.f;
        if (sync && wrap1) v->ph2 = v->ph1 * (inc2 / (inc1 > 1e-9f ? inc1 : 1e-9f));

        float mix = (s1 * g1 + s2 * g2 + sub * subv + white() * noisev + s1 * s2 * ringv + o3 * o3v) * ov * 0.5f;

        float out;
        if (route == 3) {                                  /* split: osc1 -> F1, osc2 -> F2 */
            float x1 = (s1 * g1 + sub * subv) * ov * 0.5f, x2 = (s2 * g2 + white() * noisev) * ov * 0.5f;
            float y1 = m1 < 4 ? svf_tick(&v->f1, x1, a11, a21, a31, k1, m1) : ladder_tick(&v->lad, x1, Gl, kl, m1 - 3);
            float y2 = svf_tick(&v->f2, saturate(x2, sat), a12, a22, a32, k2, m2);
            out = saturate(y1, sat) + y2;
        } else if (route == 2) {                           /* parallel */
            float y1 = m1 < 4 ? svf_tick(&v->f1, mix, a11, a21, a31, k1, m1) : ladder_tick(&v->lad, mix, Gl, kl, m1 - 3);
            float y2 = svf_tick(&v->f2, mix, a12, a22, a32, k2, m2);
            out = saturate(y1 * (1.f - fb) + y2 * fb, sat);
        } else {                                           /* serial 4 / 6 */
            float y1 = m1 < 4 ? svf_tick(&v->f1, mix, a11, a21, a31, k1, m1) : ladder_tick(&v->lad, mix, Gl, kl, m1 - 3);
            if (route == 1 && m1 < 4) y1 = svf_tick(&v->f1b, y1, a11, a21, a31, 1.41f, m1);  /* ser6: F1 at 24 dB */
            y1 = saturate(y1, sat);
            float y2 = svf_tick(&v->f2, y1, a12, a22, a32, k2, m2);
            out = y1 * (1.f - fb) + y2 * fb;
        }
        out *= amp;
        L[i] += out * gl; R[i] += out * gr;
    }
}

/* ---------- effects: chorus and delay ---------- */
#define CH_LEN 2048
#define DL_LEN 32768                     /* 0.74 s at 44.1 kHz */
static float chbuf[2][CH_LEN] HV_POOL; static int chw; static float chph;
static int16_t dlbuf[2][DL_LEN] HV_POOL; static int dlw; static float dl_lp[2];

static void fx_render(float *L, float *R, int n) {
    float mix = u7(PA_CH_MIX);
    if (mix > 0.f) {
        float rate = lfo_hz(P[PA_CH_RATE]) * 0.25f, depth = u7(PA_CH_DEPTH) * 0.004f * hv_fs;
        float dly = (0.002f + u7(PA_CH_DELAY) * 0.02f) * hv_fs, fbk = s7(PA_CH_FB) * 0.9f;
        for (int i = 0; i < n; i++) {
            chph += rate * hv_inv_fs; if (chph >= 1.f) chph -= 1.f;
            float x[2] = { L[i], R[i] };
            for (int c = 0; c < 2; c++) {
                float m = hv_sin_turn(chph + c * 0.25f);
                float d = dly + depth * (1.f + m) * 0.5f;
                float rp = chw - d; while (rp < 0) rp += CH_LEN;
                int i0 = (int)rp; float f = rp - i0;
                float y = chbuf[c][i0 & (CH_LEN - 1)] * (1.f - f) + chbuf[c][(i0 + 1) & (CH_LEN - 1)] * f;
                chbuf[c][chw] = x[c] + y * fbk;
                float o = x[c] * (1.f - mix * 0.5f) + y * mix;
                if (c) R[i] = o; else L[i] = o;
            }
            chw = (chw + 1) & (CH_LEN - 1);
        }
    }
    int mode = P[PA_DLY_MODE];
    float send = u7(PA_DLY_SEND);
    if (mode && send > 0.f) {
        /* every delay/reverb mode plays as a stereo delay for now; reverb comes in phase 3 */
        float t = (0.005f + u7(PA_DLY_TIME) * u7(PA_DLY_TIME) * 0.72f) * hv_fs;
        int d = (int)hv_clampf(t, 1.f, DL_LEN - 2);
        float fbk = u7(PA_DLY_FB) * 0.95f, col = s7(PA_DLY_COLOR);
        float lp = col < 0 ? 1.f + col * 0.9f : 1.f;
        for (int i = 0; i < n; i++) {
            float yl = dlbuf[0][(dlw - d) & (DL_LEN - 1)] * (1.f / 16384.f), yr = dlbuf[1][(dlw - d) & (DL_LEN - 1)] * (1.f / 16384.f);
            dl_lp[0] += (yl - dl_lp[0]) * lp; dl_lp[1] += (yr - dl_lp[1]) * lp;
            float hl = col > 0 ? yl - dl_lp[0] * col : dl_lp[0], hr = col > 0 ? yr - dl_lp[1] * col : dl_lp[1];
            dlbuf[0][dlw] = (int16_t)(hv_clampf(L[i] * send + hr * fbk, -1.99f, 1.99f) * 16384.f);   /* ping-pong */
            dlbuf[1][dlw] = (int16_t)(hv_clampf(R[i] * send + hl * fbk, -1.99f, 1.99f) * 16384.f);
            L[i] += hl; R[i] += hr;
            dlw = (dlw + 1) & (DL_LEN - 1);
        }
    }
}

/* ---------- API ---------- */
void hv_init(float fs) {
    hv_fs = fs; hv_inv_fs = 1.f / fs;
    memset(V, 0, sizeof V); hv_init_patch(P);
    memset(dlbuf, 0, sizeof dlbuf); memset(chbuf, 0, sizeof chbuf);
    build_waves();
}
/* a neutral starting patch: one saw, open-ish low-pass, organ-like amp */
void hv_init_patch(uint8_t p[256]) {
    memset(p, 0, 256);
    static const int centred[] = { PA_PAN, PA_O1_SEMI, PA_O1_KEYF, PA_O2_SEMI, PA_O2_FENV, PA_FM_FENV, PA_O2_KEYF,
        PA_OSC_BAL, PA_NOISE_COL, PA_F1_KEYF, PA_F2_KEYF, PA_FBAL, PA_FE_ST, PA_AE_ST, PA_L1_SYM, PA_L2_SYM,
        PA_L1_O1, PA_L1_O2, PA_L1_PW, PA_L1_RES, PA_L1_GAIN, PA_L2_SHP, PA_L2_FM, PA_L2_CUT1, PA_L2_CUT2, PA_L2_PAN,
        PA_TRANSPOSE, PA_UNI_LFOPH, PA_CH_FB, PA_DLY_COLOR, HV_B(PB_O3_SEMI), HV_B(PB_AMP_VEL) };
    for (unsigned i = 0; i < sizeof centred / sizeof *centred; i++) p[centred[i]] = 64;
    p[PA_O1_SHAPE] = 64; p[PA_O2_SHAPE] = 64; p[PA_O1_PW] = 0; p[PA_O2_PW] = 0;
    p[PA_OSC_BAL] = 0; p[PA_OSC_VOL] = 64; p[PA_CUT1] = 100; p[PA_CUT2] = 127; p[PA_FBAL] = 0;
    p[PA_SAT] = 0; p[PA_F1_MODE] = 0; p[PA_F2_MODE] = 0; p[PA_FROUTE] = 0;
    p[PA_FE_A] = 0; p[PA_FE_D] = 60; p[PA_FE_S] = 0; p[PA_FE_R] = 40;
    p[PA_AE_A] = 0; p[PA_AE_D] = 60; p[PA_AE_S] = 127; p[PA_AE_R] = 30;
    p[PA_L1_RATE] = 64; p[PA_L2_RATE] = 64; p[PA_VOL] = 100;
    p[HV_B(PB_F1_POL)] = 1; p[HV_B(PB_F2_POL)] = 1; p[HV_B(PB_AMP_VEL)] = 64 + 20;
    memcpy(p + HV_B(PB_NAME), "Init      ", 10);
}
void hv_load_patch(const uint8_t p[256]) { memcpy(P, p, 256); }
void hv_set_param(int i, uint8_t v) { if (i >= 0 && i < 256) P[i] = v & 127; }
uint8_t hv_get_param(int i) { return (i >= 0 && i < 256) ? P[i] : 0; }

static int uni_count(void) { int m = P[PA_UNI_MODE]; return m <= 0 ? 1 : m + 1 > 4 ? 4 : m + 1; }

static void start_voice(voice_t *v, int note, int vel, float detune, float pan, int legato) {
    v->note = note; v->gate = 1; v->vel = vel / 127.f; v->age = (float)++age_counter;
    v->target = (float)note;
    if (!v->on || P[PA_PORTA] == 0) v->pitch = (float)note;
    v->detune = detune; v->pan = pan;
    if (!legato) {
        v->fe.st = 1; v->ae.st = 1;
        if (!v->on) { v->fe.lvl = v->ae.lvl = 0.f; memset(&v->f1, 0, sizeof v->f1); memset(&v->f2, 0, sizeof v->f2); memset(&v->f1b, 0, sizeof v->f1b); memset(&v->lad, 0, sizeof v->lad); }
        if (P[PA_L1_KTRIG]) v->l1.ph = 0.f;
        if (P[PA_L2_KTRIG]) v->l2.ph = 0.f;
        v->ph1 = 0.f; v->ph2 = 0.25f; v->phs = 0.f;
    }
    v->on = 1;
}

void hv_note_on(int note, int vel) {
    if (vel == 0) { hv_note_off(note); return; }
    int u = uni_count();
    float spread = u7(PA_UNI_DET) * 0.3f, pspread = u7(PA_UNI_PAN);
    int mono = P[PA_KEYMODE] != 0;
    for (int k = 0; k < u; k++) {
        voice_t *best = 0;
        if (mono) best = &V[k];
        else {
            for (int i = 0; i < HV_NVOICE; i++) if (!V[i].on) { best = &V[i]; break; }
            if (!best) {                                  /* steal: oldest released, then oldest */
                for (int i = 0; i < HV_NVOICE; i++)
                    if (!V[i].gate && (!best || V[i].age < best->age)) best = &V[i];
                if (!best) for (int i = 0; i < HV_NVOICE; i++)
                    if (!best || V[i].age < best->age) best = &V[i];
            }
        }
        float pos = u > 1 ? (float)k / (u - 1) * 2.f - 1.f : 0.f;
        start_voice(best, note, vel, pos * spread, pos * pspread, mono && best->on && best->gate);
    }
}
void hv_note_off(int note) {
    for (int i = 0; i < HV_NVOICE; i++)
        if (V[i].on && V[i].gate && V[i].note == note) { V[i].gate = 0; V[i].fe.st = 4; V[i].ae.st = 4; }
}
void hv_all_off(void) { for (int i = 0; i < HV_NVOICE; i++) { V[i].gate = 0; V[i].fe.st = V[i].ae.st = 4; } }
int hv_active_voices(void) { int c = 0; for (int i = 0; i < HV_NVOICE; i++) c += V[i].on; return c; }

void hv_render(float *L, float *R, int n) {
    for (int o = 0; o < n; o += HV_BLOCK) {
        int m = n - o < HV_BLOCK ? n - o : HV_BLOCK;
        for (int i = 0; i < HV_NVOICE; i++) if (V[i].on) voice_render(&V[i], L + o, R + o, m);
    }
    fx_render(L, R, n);
}
