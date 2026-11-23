/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-hv: a Virus-inspired synthesizer for the M-VAVE FM-1. Copyright (c) 2026 Brian McCalla.
 * Built on Dinghy (zvenson), on Felucca's platform (Leo Kuroshita, Hügelton Instruments) as SLOOP carries it:
 * see LICENSING.md. (The file keeps Dinghy's name so the build and its tests stay as they are.)
 *
 *   keys          play; the notes go out over USB-MIDI too
 *   OCT- OCT+     the octave
 *   PRESETS       the patch
 *   SELECT        the edit page (OSC, MIX, FILTER, FILT ENV, AMP ENV, LFO, FX, UNISON); ENV / LFO / FX jump there
 *   K1..K4        the page's four parameters
 *   MIDI in       notes (USB, TRS), CC 17..119 = the Virus page A parameter of that number, pitch bend +-2
 *
 * The sound engine is firmware/src/hv/ (float, the FPU: -mcpu=r3). The patch is a 256-byte single in the
 * classic Virus layout, so a Virus A/B/C single plays as it is (docs/PATCHES.md). */
static void app_idle(void);
#define LCD_IDLE app_idle
#ifndef APP_NAME
#define APP_NAME "FM1-HV"
#endif
#include "platform.c"

#ifdef DINGHY_HOST                              /* (the host has no tick counter: the load reads 0 there) */
static uint32_t fm1_ticks(void) { return 0; }
#define FM1_TICKS_PER_US 24u
#endif
#define HV_IN_FIRMWARE 1
#ifndef DINGHY_HOST
#define HV_POOL __attribute__((section(".pool")))
#endif
#define HV_NVOICE 6
#include "hv/hv_engine.c"
#include "hv/hv_presets.h"

/* ---- main loop -> audio ISR: a ring of note events (one writer each side) */
typedef struct { uint8_t on, note, vel, pad; } nev_t;
static nev_t evq[64];
static volatile uint32_t evq_w, evq_r;
static void ev_push(uint32_t on, uint32_t note, uint32_t vel)
{
    uint32_t w = evq_w;
    if (w - evq_r >= 64u) return;               /* full: drop (a flood of MIDI) */
    evq[w & 63u].on = (uint8_t)on; evq[w & 63u].note = (uint8_t)note; evq[w & 63u].vel = (uint8_t)vel;
    evq_w = w + 1u;
}

static float bufL[256], bufR[256];
static volatile uint32_t load_us, load_peak_us, voices_now;

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t t0 = fm1_ticks(), i;
    while (evq_r != evq_w) {
        nev_t e = evq[evq_r & 63u];
        if (e.on) hv_note_on(e.note, e.vel); else hv_note_off(e.note);
        evq_r++;
    }
    if (n > 256u) n = 256u;
    for (i = 0; i < n; i++) bufL[i] = bufR[i] = 0.f;
    hv_render(bufL, bufR, (int)n);
    for (i = 0; i < n; i++) {
        float l = hv_clampf(bufL[i] * 30000.f, -32767.f, 32767.f), r = hv_clampf(bufR[i] * 30000.f, -32767.f, 32767.f);
        out[2u * i] += (int32_t)l;
        out[2u * i + 1u] += (int32_t)r;
    }
    t0 = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
    load_us = t0;
    if (t0 > load_peak_us) load_peak_us = t0;
    voices_now = (uint32_t)hv_active_voices();
}

/* ---- pages: four parameters each (index into the single, the label) */
typedef struct { const char *name; uint8_t idx[4]; const char *lab[4]; } page_t;
static const page_t PAGES[] = {
    {"OSC",      {PA_O1_SHAPE, PA_O2_SHAPE, PA_O2_SEMI, PA_O2_DETUNE}, {"SHAPE1", "SHAPE2", "SEMI2", "DETUNE"}},
    {"OSC MOD",  {PA_O1_PW, PA_O2_FM, PA_O2_SYNC, PA_O2_FENV},        {"PW", "FM", "SYNC", "ENV>P2"}},
    {"MIX",      {PA_OSC_BAL, PA_SUB_VOL, PA_NOISE_VOL, PA_OSC_VOL},  {"BAL", "SUB", "NOISE", "DRIVE"}},
    {"FILTER",   {PA_CUT1, PA_RES1, PA_F1_ENV, PA_F1_MODE},           {"CUTOFF", "RESO", "ENVAMT", "MODE"}},
    {"FILTER 2", {PA_CUT2, PA_RES2, PA_FROUTE, PA_SAT},               {"CUT2", "RESO2", "ROUTE", "SAT"}},
    {"FILT ENV", {PA_FE_A, PA_FE_D, PA_FE_S, PA_FE_R},                {"ATTACK", "DECAY", "SUST", "REL"}},
    {"AMP ENV",  {PA_AE_A, PA_AE_D, PA_AE_S, PA_AE_R},                {"ATTACK", "DECAY", "SUST", "REL"}},
    {"LFO 1",    {PA_L1_RATE, PA_L1_SHAPE, PA_L1_PW, PA_L1_O1},       {"RATE", "SHAPE", ">PW", ">PITCH"}},
    {"LFO 2",    {PA_L2_RATE, PA_L2_SHAPE, PA_L2_CUT1, PA_L2_PAN},    {"RATE", "SHAPE", ">CUT", ">PAN"}},
    {"CHORUS",   {PA_CH_MIX, PA_CH_RATE, PA_CH_DEPTH, PA_CH_FB},      {"MIX", "RATE", "DEPTH", "FDBK"}},
    {"DELAY",    {PA_DLY_SEND, PA_DLY_TIME, PA_DLY_FB, PA_DLY_COLOR}, {"SEND", "TIME", "FDBK", "COLOR"}},
    {"UNISON",   {PA_UNI_MODE, PA_UNI_DET, PA_UNI_PAN, PA_PORTA},     {"VOICES", "DETUNE", "SPREAD", "GLIDE"}},
};
#define NPAGE (sizeof PAGES / sizeof PAGES[0])
enum { PG_FENV = 5, PG_LFO = 7, PG_FX = 9 };

static uint8_t pmax(uint32_t idx)               /* the parameters that are not 0..127 */
{
    switch (idx) {
    case PA_O2_SYNC: case PA_SUB_SHAPE: return 1;
    case PA_F1_MODE: return 7;
    case PA_F2_MODE: case PA_FROUTE: return 3;
    case PA_SAT: return 14;
    case PA_UNI_MODE: return 3;                 /* up to 4 voices here */
    case PA_L1_SHAPE: case PA_L2_SHAPE: return 5;
    default: return 127;
    }
}

/* ---- the screen */
#define C_ACC RGB(230, 40, 60)                  /* red, a nod to the original's colour */
#define C_GRY RGB(110, 110, 110)
#define C_BAR RGB(60, 60, 60)
static void fmt_u(char *s, uint32_t v) { char t[4]; int n = 0; do t[n++] = (char)('0' + v % 10u); while (v /= 10u); while (n) *s++ = t[--n]; *s = 0; }
static void fmt_param(char *s, uint32_t idx, uint32_t v)
{
    static const char *const FM1[8] = {"LP", "HP", "BP", "BS", "1POLE", "2POLE", "3POLE", "4POLE"};
    static const char *const FM2[4] = {"LP", "HP", "BP", "BS"};
    static const char *const RT[4] = {"SER4", "SER6", "PAR", "SPLIT"};
    static const char *const LS[6] = {"SINE", "TRI", "SAW", "SQR", "S&H", "S&G"};
    const char *t = 0;
    if (idx == PA_F1_MODE) t = FM1[v & 7u];
    else if (idx == PA_F2_MODE) t = FM2[v & 3u];
    else if (idx == PA_FROUTE) t = RT[v & 3u];
    else if ((idx == PA_L1_SHAPE || idx == PA_L2_SHAPE) && v < 6u) t = LS[v];
    else if (idx == PA_O2_SYNC) t = v ? "ON" : "OFF";
    else if (idx == PA_UNI_MODE) { if (!v) t = "OFF"; else { s[0] = (char)('1' + v); s[1] = 0; return; } }
    if (t) { while (*t) *s++ = *t++; *s = 0; return; }
    if (idx == PA_O2_SEMI || idx == PA_OSC_BAL || idx == PA_L1_PW || idx == PA_L1_O1 || idx == PA_L2_CUT1 ||
        idx == PA_L2_PAN || idx == PA_O2_FENV || idx == PA_CH_FB || idx == PA_DLY_COLOR) {
        int32_t x = (int32_t)v - 64;
        *s++ = x < 0 ? '-' : '+';
        fmt_u(s, (uint32_t)(x < 0 ? -x : x));
        return;
    }
    fmt_u(s, v);
}

static void draw_header(uint32_t preset)
{
    char name[11];
    uint32_t i;
    for (i = 0; i < 10u; i++) { char c = (char)hv_get_param(HV_B(PB_NAME) + (int)i); name[i] = c >= 32 && c < 127 ? c : ' '; }
    name[10] = 0;
    {
        char num[8] = "";
        fmt_u(num, preset + 1u);
        draw_text_box(0, 8, 240, &FONT_S, num, C_GRY, 0);
    }
    draw_text_box(0, 30, 240, &FONT_L, name, C_WHITE, 1);
}

static void draw_page(uint32_t pg)
{
    uint32_t k;
    draw_text_box(0, 74, 240, &FONT_S, PAGES[pg].name, C_ACC, 1);
    for (k = 0; k < 4u; k++) {
        uint32_t idx = PAGES[pg].idx[k], v = hv_get_param((int)idx), mx = pmax(idx);
        char val[12];
        int32_t x = 4 + (int32_t)k * 59, h = (int32_t)((v * 70u) / (mx ? mx : 1u));
        fmt_param(val, idx, v);
        cv_begin(56, 64, C_BLACK);
        cv_rect(20, 0, 16, 70 - 6, C_BAR);
        if (h > 64) h = 64;
        cv_rect(20, 64 - h, 16, h, C_ACC);
        cv_blit(x, 100);
        draw_text_box(x - 2, 168, 60, &FONT_S, PAGES[pg].lab[k], C_GRY, 1);
        draw_text_box(x - 2, 186, 60, &FONT_S, val, C_WHITE, 1);
    }
}

static void draw_status(int32_t oct)
{
    char s[40] = "OCT +0   CPU ", *p;
    uint32_t pct = (load_peak_us * 100u) / 5805u;   /* of the ISR's 5.8 ms */
    s[4] = oct < 0 ? '-' : '+';
    s[5] = (char)('0' + (oct < 0 ? -oct : oct));
    p = s + 13;
    fmt_u(p, pct); while (*p) p++;
    *p++ = '%'; *p++ = ' '; *p++ = ' '; *p++ = 'V'; *p++ = ' ';
    fmt_u(p, voices_now);
    draw_text_box(0, 216, 240, &FONT_S, s, C_GRY, 1);
}

/* ---- MIDI */
static uint32_t idle_ok;
static volatile uint32_t page_dirty;
static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2) != 0) {
        uint32_t ty = st & 0xF0u;
        if (ty == 0x90u && d2) ev_push(1, d1, d2);
        else if (ty == 0x80u || ty == 0x90u) ev_push(0, d1, 0);
        else if (ty == 0xB0u) {
            if (d1 == 120u || d1 == 123u) hv_all_off();
            else if (d1 >= 17u && d1 <= 119u && d1 != 32u && d1 != 64u && d1 != 65u && d1 != 66u) { hv_set_param(d1, d2); page_dirty = 1; }
        }
    }
}
static void app_idle(void) { if (idle_ok) midi_in(); }

static void load_preset(uint32_t i)
{
    uint8_t p[256];
    hv_init_patch(p);
    HV_PRESETS[i % HV_NPRESET].make(p);
    hv_all_off();
    hv_load_patch(p);
}

static void app_main(void)
{
    uint32_t prev = 0, k, preset = 0, pg = 3, drawn_preset = ~0u, drawn_pg = ~0u, t_stat = 0;
    int32_t oct = 0, e;
    uint8_t held[27] = {0};
    hv_init(44100.f);
    load_preset(0);
    lcd_fill(0, 0, 240, 240, C_BLACK);

    for (;;) {
        uint32_t keys, down, up, rel, b, base;
        platform_poll();
        base = (uint32_t)(53 + 12 * oct);
        keys = fm1_in.notes; down = keys & ~prev; up = prev & ~keys; prev = keys;
        for (k = 0; k < 27u; k++) {
            if (down >> k & 1u) { held[k] = (uint8_t)(base + k); ev_push(1, held[k], 100); midi_send(0x90u, held[k], 100); }
            if (up >> k & 1u) { ev_push(0, held[k], 0); midi_send(0x80u, held[k], 0); }
            fm1_led_key(KEY_LED(k), keys >> k & 1u);
        }
        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_OCT_DOWN) && oct > -3) oct--;
        if (b & (1u << BTN_OCT_UP) && oct < 3) oct++;
        if (b & (1u << BTN_ENV)) pg = pg == PG_FENV ? PG_FENV + 1u : PG_FENV;
        if (b & (1u << BTN_LFO)) pg = pg == PG_LFO ? PG_LFO + 1u : PG_LFO;
        if (b & (1u << BTN_FX)) pg = pg == PG_FX ? PG_FX + 1u : PG_FX;
        if ((e = fm1_enc_take(ENC_SELECT)) != 0) pg = (uint32_t)(((int32_t)pg + e + (int32_t)NPAGE * 8) % (int32_t)NPAGE);
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0) {
            preset = (uint32_t)(((int32_t)preset + e + HV_NPRESET * 8) % HV_NPRESET);
            load_preset(preset);
        }
        for (k = 0; k < 4u; k++)
            if ((e = fm1_enc_take(ENC_KNOB1 + (int)k)) != 0) {
                uint32_t idx = PAGES[pg].idx[k], mx = pmax(idx);
                int32_t step = mx > 15u && (e > 1 || e < -1) ? e * 3 : e;   /* a fast turn moves further */
                int32_t v = (int32_t)hv_get_param((int)idx) + step;
                hv_set_param((int)idx, (uint8_t)(v < 0 ? 0 : v > (int32_t)mx ? (int32_t)mx : v));
                page_dirty = 1;
            }
        fm1_led_key(BTN_OCT_DOWN, oct < 0);
        fm1_led_key(BTN_OCT_UP, oct > 0);
        fm1_led_key(BTN_ENV, pg == PG_FENV || pg == PG_FENV + 1u);
        fm1_led_key(BTN_LFO, pg == PG_LFO || pg == PG_LFO + 1u);
        fm1_led_key(BTN_FX, pg == PG_FX || pg == PG_FX + 1u);
        midi_in();

        idle_ok = 1;
        if (preset != drawn_preset) { drawn_preset = preset; draw_header(preset); page_dirty = 1; }
        if (pg != drawn_pg || page_dirty) { drawn_pg = pg; page_dirty = 0; draw_page(pg); }
        if (fm1_ms - t_stat > 500u) { t_stat = fm1_ms; draw_status(oct); load_peak_us = load_us; }
        idle_ok = 0;
    }
}
