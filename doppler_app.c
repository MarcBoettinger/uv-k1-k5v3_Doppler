/* Doppler overlay app for F4HWN Labs ABI 1 / API 3.
 *
 * Simulates and tracks the Doppler shift of a moving transmitter (ISS, aircraft)
 * on a stationary receiver: f = f0 (1 +/- v/c). RX only: no transmit service of
 * the API is used anywhere, and PTT is ignored.
 *
 * Tuning: the VFO's RF path and filter are left as the loader set them, so the
 * receiver is only really retuned within +/-2 MHz of the VFO frequency; outside
 * that window the screen shows SIM and the app is a pure calculator. */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"
#include "doppler_core.h"

#define TICK_MS      40u
#define MUTE_MS      40u          /* simulated dropout after every retune     */
#define REFRESH_MS   1000u
#define LIVE_WINDOW  200000u      /* 10 Hz units = 2 MHz around the VFO       */
#define SQL_DBM      (-115)       /* audio opens above this RSSI              */
#define REG_FREQ_LO  0x38u
#define REG_FREQ_HI  0x39u
#define REG_CTRL     0x30u

static struct {
    dop_t s;
    const app_api_t *api;
    uint32_t vfo;                 /* VFO frequency at launch, 10 Hz units     */
    uint32_t drawnAt, muteAt, riseAt, passAt;
    int32_t age;                  /* ms since rise (negative while waiting)  */
    uint16_t ctrl, repeatMs;      /* REG_30 as found, key repeat countdown    */
    uint8_t prevKey, rep, beeps;
    bool running, saver, dirty, muted, afOpen;
} g;
#define A (g.api)

/* The resident 32-bit division helpers; libgcc's would not fit the overlay. */
uint64_t __aeabi_uidivmod(uint32_t n, uint32_t d) { return g.api->uidivmod(n, d); }
uint64_t __aeabi_uidiv(uint32_t n, uint32_t d) __attribute__((alias("__aeabi_uidivmod")));
uint64_t __aeabi_idivmod(int32_t n, int32_t d) { return g.api->idivmod(n, d); }
uint64_t __aeabi_idiv(int32_t n, int32_t d) __attribute__((alias("__aeabi_idivmod")));

/* ---------------------------------------------------------------- radio */

static uint32_t rx10(void)        /* receiver frequency, 10 Hz units (what the BK4819 resolves) */
{
    return (uint32_t)((int32_t)g.s.f0 + g.s.dial + g.s.rit + 5) / 10u;
}

static bool live(void)
{
    const uint32_t f = rx10();
    return (f > g.vfo ? f - g.vfo : g.vfo - f) <= LIVE_WINDOW;
}

static void tune(uint32_t f)
{
    void (*const w)(uint8_t, uint16_t) = A->bk_write;
    w(REG_FREQ_LO, (uint16_t)f);
    w(REG_FREQ_HI, (uint16_t)(f >> 16));
    w(REG_CTRL, 0);
    w(REG_CTRL, g.ctrl);
}

static void retune(void)         /* every real retune drops the audio for MUTE_MS */
{
    if (live()) {
        A->set_af(APP_AF_MUTE);
        g.afOpen = false;
        tune(rx10());
    }
    g.muted = true;
    g.muteAt = A->ticks_ms();
}

static void audio(void)          /* open the speaker only on a real signal */
{
    const bool open = !g.muted && live() && A->rssi_dbm() >= SQL_DBM;
    if (open != g.afOpen) {
        g.afOpen = open;
        A->set_af(open ? APP_AF_FM : APP_AF_MUTE);
    }
}

/* ------------------------------------------------------------------ pass */

static void blip(uint16_t hz, uint16_t ms)   /* same sequence as FoxHunt: tone out, then RX back on */
{
    A->prepare_tone();
    A->play_tone_raw(hz, ms);
    A->tones_off_rx();
    A->set_af(APP_AF_MUTE);
    g.afOpen = false;
}

static void pass_update(uint32_t now)
{
    dop_t *s = &g.s;
    uint8_t rec;
    g.age = (int32_t)(now - g.riseAt);
    g.dirty = true;
    if (g.age >= (int32_t)((uint32_t)s->durS * 1000u)) {     /* pass over: stay where we are */
        s->mode = DC_RUN;
        blip(600, 300);
        return;
    }
    if (g.age < 0) {
        if (g.age >= -60000 && !(g.beeps & 1u)) { g.beeps |= 1u; blip(1000, 100); }
    } else if (!(g.beeps & 4u)) {
        g.beeps |= 4u;
        blip(1500, 400);                             /* rise */
    }
    s->vcms = dc_pass_vcms(s->emax, s->durS, g.age < 0 ? 0u : (uint32_t)g.age, &rec);
    s->recede = rec;
    if (dc_follow(s, 0) & R_TUNE) retune();
}

/* ---------------------------------------------------------------- render */

static char *cat(char *o, const char *t) { while (*t) *o++ = *t++; return o; }

static char *dec(char *o, uint32_t v, uint8_t w)      /* zero-padded to w digits */
{
    uint8_t i;
    for (i = w; i--; v /= 10u) o[i] = (char)('0' + v % 10u);
    return o + w;
}

static char *sgn(char *o, int32_t v)
{
    if (v >= 0) *o++ = '+';
    return A->append_i32(o, v);
}

static char *mhz(char *o, uint32_t hz)
{
    o = A->append_u32(o, hz / 1000000u);
    *o++ = '.';
    return dec(o, hz % 1000000u, 6);
}

static char *field(char *o, uint8_t slots, bool dot)   /* typed digits, '_' for the rest */
{
    uint8_t i;
    for (i = 0; i < slots; i++) {
        if (dot && i == 3u) *o++ = '.';
        *o++ = i < g.s.en ? (char)('0' + g.s.ed[i]) : '_';
    }
    return o;
}

static void line(uint8_t n, char *o, char *e)
{
    *e = 0;
    A->print_normal(o, 0, 0, n);
}

static char *hms(char *o, uint32_t sec)
{
    o = A->append_u32(o, sec / 3600u);
    *o++ = ':';
    o = dec(o, (sec / 60u) % 60u, 2);
    *o++ = ':';
    return dec(o, sec % 60u, 2);
}

static void render_setup(void)
{
    static const char lab[3][10] = { " RISE IN ", " DUR. MIN", " MAX ELEV" };
    char b[24], *o;
    uint8_t n;
    A->print_normal("PASS SETUP", 0, 0, 0);
    for (n = 0; n < 3u; n++) {
        const bool typed = n == g.s.pf && g.s.en;
        const uint32_t v = dc_typed(&g.s);
        o = cat(b, lab[n]);
        b[0] = n == g.s.pf ? '>' : ' ';
        if (n == 0) o = hms(o, typed ? dc_hms(v) : g.s.riseS);
        else o = dec(o, typed ? v : n == 1 ? (uint32_t)g.s.durS / 60u : g.s.emax, 2);
        line(1u + n, b, o);
    }
    A->print_normal("MENU=ok EXIT=del", 0, 0, 6);
}

static void render(void)
{
    const dop_t *s = &g.s;
    const int32_t delta = dc_delta(s->f0, s->vcms, s->recede);
    const int32_t err = s->dial + s->rit - delta;
    char b[24], *o;
    uint8_t i, j, q = g.muted ? 0 : dc_quality(err);

    A->display_clear();
    if (s->mode == DC_PSET) { render_setup(); return; }

    if (s->mode == DC_PASS && g.age < 0) {
        o = hms(cat(b, "T-"), (uint32_t)(-g.age + 999) / 1000u);
    } else if (s->mode == DC_PASS) {
        o = hms(cat(b, "P "), (uint32_t)g.age / 1000u);
        o = cat(o, g.muted ? " LOST" : s->recede ? " RECD" : " APPR");
    } else {
        o = cat(b, "DOPPLER ");
        o = cat(o, g.muted ? "LOST" : !live() ? "SIM" : s->recede ? "RECD" : "APPR");
    }
    line(0, b, o);
    for (i = 0; i < 4u; i++) {                              /* signal bars, top right */
        const uint8_t h = (uint8_t)(2u * (i + 1u));
        for (j = 0; j < 5u; j++)
            A->fb[0][100u + i * 7u + j] = i < q ? (uint8_t)(0xFFu << (8u - h)) : 0x80u;
    }

    o = cat(b, "F0 ");
    o = s->mode == DC_EDF ? field(o, 8, true) : mhz(o, s->f0);
    line(1, b, cat(o, "MHz"));

    o = cat(b, "V ");
    if (s->mode == DC_EDV) {
        o = field(o, 5, false);
    } else {
        *o++ = s->recede ? '-' : '+';
        o = A->append_u32(o, dc_vshow(s));
    }
    line(2, b, cat(o, s->unit_ms ? " m/s" : " km/h"));

    o = cat(b, "dF");  o = sgn(o, delta);
    o = cat(o, " E");  line(3, b, sgn(o, err));

    o = cat(b, "RX ");
    line(4, b, mhz(o, (uint32_t)((int32_t)s->f0 + s->dial + s->rit)));

    for (i = 4; i < 124u; i++) A->fb[5][i] = 0x18;           /* tuning meter: block = where the signal sits */
    A->fb[5][64] = 0x7E;
    if (!g.muted) {
        int32_t e = -err / 50;
        if (e > 58) e = 58;
        if (e < -58) e = -58;
        for (j = (uint8_t)(62 + e); j <= (uint8_t)(66 + e); j++) A->fb[5][j] = 0xFF;
    }

    o = cat(b, "S:");
    o = cat(A->append_u32(o, dc_step[s->step]), "Hz R:");
    line(6, b, sgn(o, s->rit));
}

/* ---------------------------------------------------------------- input */

static void act(uint8_t key)
{
    const uint8_t r = dc_key(&g.s, key, A->nav_dir(key), g.rep);
    const uint32_t now = A->ticks_ms();
    if (r & R_EXIT) g.running = false;
    if (r & R_TUNE) retune();
    if (r & R_START) { g.riseAt = now + g.s.riseS * 1000u; g.beeps = 0; g.passAt = now; }
    if (r & R_PEAK)  g.riseAt = now - (uint32_t)g.s.durS * 500u;   /* "now" is the closest approach */
    if (r & (R_START | R_PEAK)) pass_update(now);
    if (r) g.dirty = true;
}

static void poll_key(void)
{
    uint8_t key = A->get_key();
    if (key == APP_KEY_SAVER) { g.saver = true; g.prevKey = APP_KEY_INVALID; g.repeatMs = 0; return; }
    if (key == APP_KEY_WAKE || key == APP_KEY_PTT) {        /* PTT only wakes the screen: never transmits */
        g.saver = false; g.dirty = true; key = APP_KEY_INVALID;
    }
    if (key == APP_KEY_INVALID) { g.prevKey = key; g.repeatMs = 0; g.rep = 0; return; }
    if (key != g.prevKey) {
        A->backlight_on();
        g.rep = 0;
        act(key);
        g.repeatMs = 260u;
    } else if (g.repeatMs > TICK_MS) {
        g.repeatMs -= TICK_MS;
    } else {
        if (g.rep < 255u) g.rep++;
        act(key);
        g.repeatMs = 80u;
    }
    g.prevKey = key;
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    A = api;
    g.vfo = A->rx_freq();
    g.s.f0 = g.vfo * 10u;                                   /* start on the VFO; MENU edits it */
    g.s.vcms = 700000u;                                     /* 7 km/s, ISS-like */
    g.s.unit_ms = 1;
    g.s.step = 2;
    g.s.durS = 600u;                                        /* pass setup defaults */
    g.s.emax = 40;
    g.ctrl = A->bk_read(REG_CTRL);
    g.prevKey = A->get_key();                               /* a key still held from the launcher is ignored */
    g.running = true;
    g.dirty = true;
    A->backlight_on();
    A->audio_path(true);
    A->set_af(APP_AF_MUTE);

    while (g.running) {
        uint32_t now;
        poll_key();
        if (!g.running) break;
        now = A->ticks_ms();
        if (g.muted && now - g.muteAt >= MUTE_MS) { g.muted = false; g.dirty = true; }
        if (g.s.mode == DC_PASS && now - g.passAt >= 500u) { g.passAt = now; pass_update(now); }
        audio();
        if (!g.saver && (g.dirty || now - g.drawnAt >= REFRESH_MS)) {
            render();
            A->blit_status();
            A->blit_full();
            g.dirty = false;
            g.drawnAt = now;
        }
        A->battery_sample();
        A->backlight_update();
        A->delay_ms(TICK_MS);
    }

    A->set_af(APP_AF_MUTE);
    tune(g.vfo);                                            /* leave the PLL where the VFO had it */
    A->audio_path(false);
}
