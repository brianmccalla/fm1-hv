/* render.c -- host test: plays demo patches through the engine and writes WAV files.
 * Also reports render cost per voice on this computer (only a rough guide to the FM-1).
 * Copyright (c) 2026 Brian McCalla. GPL-3.0-only. */
#include "../../firmware/src/hv/hv_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FSR 44100
static void wav_write(const char *path, const float *L, const float *R, int n) {
    FILE *f = fopen(path, "wb"); if (!f) { perror(path); exit(1); }
    int data = n * 4, v;
    fwrite("RIFF", 1, 4, f); v = 36 + data; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); short s = 1; fwrite(&s, 2, 1, f); s = 2; fwrite(&s, 2, 1, f);
    v = FSR; fwrite(&v, 4, 1, f); v = FSR * 4; fwrite(&v, 4, 1, f); s = 4; fwrite(&s, 2, 1, f);
    s = 16; fwrite(&s, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (int i = 0; i < n; i++) {
        float a = L[i], b = R[i];
        a = a > 1 ? 1 : a < -1 ? -1 : a; b = b > 1 ? 1 : b < -1 ? -1 : b;
        short x = (short)(a * 32767), y = (short)(b * 32767);
        fwrite(&x, 2, 1, f); fwrite(&y, 2, 1, f);
    }
    fclose(f);
}

typedef struct { float t, len; int note, vel; } ev_t;
typedef struct { const char *name; void (*make)(uint8_t *); const ev_t *seq; int nev; float secs; } demo_t;

#include "../../firmware/src/hv/hv_presets.h"
/* --- phrases --- */
static const ev_t lead_seq[] = {
    {0.00f,.30f,69,100},{.375f,.30f,72,90},{.75f,.30f,76,100},{1.125f,.30f,74,90},{1.5f,.60f,72,100},
    {2.25f,.30f,71,90},{2.625f,.30f,72,100},{3.0f,1.2f,69,110}};
static const ev_t bass_seq[] = {
    {0,.1f,45,100},{.1875f,.1f,45,90},{.375f,.15f,57,120},{.5625f,.1f,45,90},{.75f,.1f,48,100},
    {.9375f,.2f,52,120},{1.125f,.1f,45,90},{1.3125f,.15f,55,110},{1.5f,.1f,45,100},{1.6875f,.1f,57,120},
    {1.875f,.1f,45,90},{2.0625f,.2f,60,127},{2.25f,.1f,45,100},{2.4375f,.1f,43,90},{2.625f,.25f,55,120},{2.8125f,.1f,45,100}};
static const ev_t pad_seq[] = {{0,3.0f,57,90},{0,3.0f,60,90},{0,3.0f,64,90},{0,3.0f,69,90}};
static const ev_t pluck_seq[] = {
    {0,.2f,69,100},{.1875f,.2f,76,90},{.375f,.2f,81,100},{.5625f,.2f,76,90},{.75f,.2f,72,100},{.9375f,.2f,79,90},
    {1.125f,.2f,84,100},{1.3125f,.2f,79,90},{1.5f,.2f,71,100},{1.6875f,.2f,76,90},{1.875f,.2f,83,100},{2.0625f,.2f,76,90}};

int main(int argc, char **argv) {
    const char *outdir = argc > 1 ? argv[1] : ".";
    demo_t demos[] = {
        {"trance_lead", trance_lead, lead_seq, 8, 6.f},
        {"acid_bass", acid_bass, bass_seq, 16, 4.f},
        {"pwm_pad", pwm_pad, pad_seq, 4, 6.5f},
        {"sync_lead", sync_lead, lead_seq, 8, 6.f},
        {"fm_pluck", fm_pluck, pluck_seq, 12, 4.f},
    };
    hv_init(FSR);
    for (unsigned d = 0; d < sizeof demos / sizeof *demos; d++) {
        uint8_t p[256]; hv_init_patch(p); demos[d].make(p); hv_load_patch(p); hv_all_off();
        int n = (int)(demos[d].secs * FSR); n -= n % HV_BLOCK;
        float *L = calloc(n, 4), *R = calloc(n, 4);
        int blk = 256; double cpu = 0; long vblocks = 0;
        for (int o = 0; o < n; o += blk) {
            float t0 = (float)o / FSR, t1 = (float)(o + blk) / FSR;
            for (int e = 0; e < demos[d].nev; e++) {
                const ev_t *ev = &demos[d].seq[e];
                if (ev->t >= t0 && ev->t < t1) hv_note_on(ev->note, ev->vel);
                if (ev->t + ev->len >= t0 && ev->t + ev->len < t1) hv_note_off(ev->note);
            }
            int m = n - o < blk ? n - o : blk;
            struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
            hv_render(L + o, R + o, m);
            clock_gettime(CLOCK_MONOTONIC, &b);
            cpu += (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) * 1e-9;
            vblocks += hv_active_voices();
        }
        char path[512]; snprintf(path, sizeof path, "%s/%s.wav", outdir, demos[d].name);
        wav_write(path, L, R, n);
        float peak = 0; for (int i = 0; i < n; i++) { float a = L[i] < 0 ? -L[i] : L[i]; if (a > peak) peak = a; }
        double avgv = (double)vblocks / ((n + blk - 1) / blk);
        printf("%-12s peak %.2f  avg voices %.1f  host load %.2f%% of real time\n",
               demos[d].name, peak, avgv, 100.0 * cpu / demos[d].secs);
        free(L); free(R);
    }
    return 0;
}
