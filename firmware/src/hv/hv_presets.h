/* hv_presets.h -- the built-in patches (our own designs, in the Virus single layout).
 * Each starts from hv_init_patch(). Copyright (c) 2026 Brian McCalla. GPL-3.0-only. */
#ifndef HV_PRESETS_H
#define HV_PRESETS_H
#include "hv_engine.h"
/* --- demo patches (our own designs, in the Virus single layout) --- */
static void trance_lead(uint8_t *p) {
        p[PA_O2_DETUNE] = 38; p[PA_OSC_BAL] = 64; p[PA_SUB_VOL] = 30;
    p[PA_UNI_MODE] = 2; p[PA_UNI_DET] = 70; p[PA_UNI_PAN] = 127;
    p[PA_CUT1] = 78; p[PA_RES1] = 30; p[PA_F1_ENV] = 40; p[PA_FE_D] = 70; p[PA_FE_S] = 50;
    p[PA_AE_A] = 2; p[PA_AE_R] = 55;
    p[PA_CH_MIX] = 35; p[PA_CH_RATE] = 40; p[PA_CH_DEPTH] = 60; p[PA_CH_DELAY] = 30;
    p[PA_DLY_MODE] = 1; p[PA_DLY_SEND] = 45; p[PA_DLY_TIME] = 62; p[PA_DLY_FB] = 55; p[PA_DLY_COLOR] = 40;
    memcpy(p + HV_B(PB_NAME), "TranceLead", 10);
}
static void acid_bass(uint8_t *p) {
        p[PA_KEYMODE] = 1; p[PA_PORTA] = 25; p[PA_O1_SEMI] = 64 - 12; p[PA_O2_SEMI] = 64 - 12; p[PA_OSC_BAL] = 0;
    p[PA_F1_MODE] = 7; p[PA_CUT1] = 34; p[PA_RES1] = 105; p[PA_F1_ENV] = 70;
    p[PA_FE_D] = 48; p[PA_FE_S] = 0; p[PA_SAT] = 2; p[PA_OSC_VOL] = 92; p[PA_FBAL] = 0;
    p[PA_AE_D] = 70; p[PA_AE_S] = 100; p[PA_AE_R] = 10;
    p[PA_DLY_MODE] = 1; p[PA_DLY_SEND] = 20; p[PA_DLY_TIME] = 50; p[PA_DLY_FB] = 30;
    memcpy(p + HV_B(PB_NAME), "Acid Bass ", 10);
}
static void pwm_pad(uint8_t *p) {
        p[PA_O1_SHAPE] = 127; p[PA_O1_PW] = 40; p[PA_L1_PW] = 64 + 30; p[PA_L1_RATE] = 40; p[PA_L1_SHAPE] = 1;
    p[PA_O2_SEMI] = 64 + 12; p[PA_O2_DETUNE] = 25; p[PA_OSC_BAL] = 50;
    p[PA_CUT1] = 70; p[PA_RES1] = 20; p[PA_F1_ENV] = 25; p[PA_FE_A] = 75; p[PA_FE_D] = 90; p[PA_FE_S] = 60; p[PA_FE_R] = 80;
    p[PA_AE_A] = 72; p[PA_AE_D] = 90; p[PA_AE_S] = 110; p[PA_AE_R] = 85;
    p[PA_L2_CUT1] = 64 + 10; p[PA_L2_RATE] = 30;
    p[PA_CH_MIX] = 70; p[PA_CH_RATE] = 30; p[PA_CH_DEPTH] = 90; p[PA_CH_DELAY] = 50; p[PA_CH_FB] = 64 + 20;
    p[PA_DLY_MODE] = 1; p[PA_DLY_SEND] = 35; p[PA_DLY_TIME] = 80; p[PA_DLY_FB] = 60; p[PA_DLY_COLOR] = 30;
    memcpy(p + HV_B(PB_NAME), "PWM Pad   ", 10);
}
static void sync_lead(uint8_t *p) {
        p[PA_O2_SYNC] = 1; p[PA_O2_SEMI] = 64 + 7; p[PA_O2_FENV] = 64 + 40; p[PA_OSC_BAL] = 110;
    p[PA_FE_D] = 55; p[PA_FE_S] = 20;
    p[PA_CUT1] = 95; p[PA_RES1] = 15; p[PA_F1_ENV] = 20; p[PA_SAT] = 3; p[PA_OSC_VOL] = 80;
    p[PA_KEYMODE] = 1; p[PA_PORTA] = 20; p[PA_L1_O1] = 64 + 1; p[PA_L1_O2] = 64 + 1; p[PA_L1_RATE] = 75;
    p[PA_DLY_MODE] = 1; p[PA_DLY_SEND] = 40; p[PA_DLY_TIME] = 55; p[PA_DLY_FB] = 45;
    memcpy(p + HV_B(PB_NAME), "Sync Lead ", 10);
}
static void fm_pluck(uint8_t *p) {
        p[PA_O1_SHAPE] = 0; p[PA_O2_SHAPE] = 0; p[PA_O2_SEMI] = 64 + 12; p[PA_OSC_BAL] = 127;
    p[PA_O2_FM] = 30; p[PA_FM_FENV] = 64 + 50; p[PA_FE_D] = 45; p[PA_FE_S] = 0;
    p[PA_CUT1] = 110; p[PA_AE_D] = 62; p[PA_AE_S] = 0; p[PA_AE_R] = 50;
    p[PA_DLY_MODE] = 1; p[PA_DLY_SEND] = 50; p[PA_DLY_TIME] = 70; p[PA_DLY_FB] = 60; p[PA_DLY_COLOR] = 64 + 30;
    p[PA_CH_MIX] = 25; p[PA_CH_RATE] = 40; p[PA_CH_DEPTH] = 50;
    memcpy(p + HV_B(PB_NAME), "FM Pluck  ", 10);
}

static void init_patch(uint8_t *p) { (void)p; }

typedef struct { const char *name; void (*make)(uint8_t *); } hv_preset_t;
static const hv_preset_t HV_PRESETS[] = {
    {"TranceLead", trance_lead}, {"Acid Bass", acid_bass}, {"PWM Pad", pwm_pad},
    {"Sync Lead", sync_lead}, {"FM Pluck", fm_pluck}, {"Init", init_patch},
};
#define HV_NPRESET ((int)(sizeof HV_PRESETS / sizeof HV_PRESETS[0]))
#endif
