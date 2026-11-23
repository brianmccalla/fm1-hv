/* hv_engine.h -- the voice engine of fm1-hv, a Virus-inspired synth for the M-VAVE FM-1.
 *
 * The native patch is a 256-byte "single" laid out as the classic Virus single dump
 * (page A = bytes 0..127, page B = bytes 128..255), so an imported Virus A/B/C single
 * plays without translation; the engine interprets the values its own way.
 *
 * Copyright (c) 2026 Brian McCalla. GPL-3.0-only.
 * Not affiliated with Access Music. "Virus" is a trademark of its owner, used here only
 * to describe patch compatibility.
 */
#ifndef HV_ENGINE_H
#define HV_ENGINE_H
#include <stdint.h>

#ifndef HV_NVOICE
#define HV_NVOICE 8          /* voice pool (unison takes several) */
#endif
#define HV_BLOCK 32          /* control-rate block, samples */

/* page A / page B indices of the parameters the engine reads (Virus C single layout) */
enum {
    PA_PORTA = 5, PA_PAN = 10,
    PA_O1_SHAPE = 17, PA_O1_PW, PA_O1_WAVE, PA_O1_SEMI, PA_O1_KEYF,
    PA_O2_SHAPE = 22, PA_O2_PW, PA_O2_WAVE, PA_O2_SEMI, PA_O2_DETUNE, PA_O2_FM, PA_O2_SYNC,
    PA_O2_FENV, PA_FM_FENV, PA_O2_KEYF,
    PA_OSC_BAL = 33, PA_SUB_VOL, PA_SUB_SHAPE, PA_OSC_VOL, PA_NOISE_VOL, PA_RING_VOL, PA_NOISE_COL,
    PA_CUT1 = 40, PA_CUT2, PA_RES1, PA_RES2, PA_F1_ENV, PA_F2_ENV, PA_F1_KEYF, PA_F2_KEYF,
    PA_FBAL = 48, PA_SAT = 49, PA_F1_MODE = 51, PA_F2_MODE, PA_FROUTE,
    PA_FE_A = 54, PA_FE_D, PA_FE_S, PA_FE_ST, PA_FE_R,
    PA_AE_A = 59, PA_AE_D, PA_AE_S, PA_AE_ST, PA_AE_R,
    PA_L1_RATE = 67, PA_L1_SHAPE, PA_L1_ENVM, PA_L1_MODE, PA_L1_SYM, PA_L1_KEYF, PA_L1_KTRIG,
    PA_L1_O1, PA_L1_O2, PA_L1_PW, PA_L1_RES, PA_L1_GAIN,
    PA_L2_RATE = 79, PA_L2_SHAPE, PA_L2_ENVM, PA_L2_MODE, PA_L2_SYM, PA_L2_KEYF, PA_L2_KTRIG,
    PA_L2_SHP, PA_L2_FM, PA_L2_CUT1, PA_L2_CUT2, PA_L2_PAN,
    PA_VOL = 91, PA_TRANSPOSE = 93, PA_KEYMODE = 94,
    PA_UNI_MODE = 97, PA_UNI_DET, PA_UNI_PAN, PA_UNI_LFOPH,
    PA_CH_MIX = 105, PA_CH_RATE, PA_CH_DEPTH, PA_CH_DELAY, PA_CH_FB, PA_CH_SHAPE,
    PA_DLY_MODE = 112, PA_DLY_SEND, PA_DLY_TIME, PA_DLY_FB, PA_DLY_RATE, PA_DLY_DEPTH,
    PA_DLY_SHAPE, PA_DLY_COLOR,
};
enum { PB_F1_POL = 30, PB_F2_POL = 31, PB_F2_LINK = 32,
       PB_O3_MODE = 41, PB_O3_VOL, PB_O3_SEMI, PB_O3_DET,
       PB_AMP_VEL = 60, PB_NAME = 112 };
#define HV_B(i) (128 + (i))

void hv_init(float fs);
void hv_init_patch(uint8_t p[256]);
void hv_load_patch(const uint8_t p[256]);
void hv_set_param(int index, uint8_t value);       /* 0..255, page A then page B */
uint8_t hv_get_param(int index);
void hv_note_on(int note, int vel);
void hv_note_off(int note);
void hv_all_off(void);
/* adds n samples (a multiple of HV_BLOCK) to L and R, full scale +-1 */
void hv_render(float *L, float *R, int n);
int hv_active_voices(void);

#endif
