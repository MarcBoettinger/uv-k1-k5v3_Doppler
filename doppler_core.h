/* Doppler overlay app - pure logic, no firmware calls (host-testable).
 *
 * f = f0 (1 +/- v/c), evaluated in 32-bit integers only: the Cortex-M0+ has no
 * divide and no FPU, and the overlay API offers 32-bit division helpers only.
 * Precision: v/c is carried as ppb x4 (0.25 ppb), so the shift is exact to
 * +/-1 Hz up to 1.3 GHz. */
#ifndef DOPPLER_CORE_H
#define DOPPLER_CORE_H

#include <stdint.h>
#include <stdbool.h>

#define DC_F_MIN     18000000u
#define DC_F_MAX     999999990u      /* 8 typed digits: ddd.ddddd MHz */
#define DC_DIAL_MAX  400000
#define DC_RIT_MAX   5000
#define DC_M         139907u         /* round(4e9 / 29979245800 * 2^20) */

#define DC_THR       200             /* pass mode: retune when the dial is this far off, Hz */

enum { DC_RUN, DC_EDF, DC_EDV, DC_PSET, DC_PASS };           /* mode            */
enum { R_TUNE = 1, R_DRAW = 2, R_EXIT = 4, R_START = 8,      /* key result bits */
       R_PEAK = 16, R_STOP = 32 };

typedef struct {
    uint32_t f0;          /* carrier, Hz                               */
    uint32_t vcms;        /* |relative velocity|, cm/s (unit-free)     */
    int32_t  dial;        /* VFO fine tune offset from f0, Hz          */
    int32_t  adj;         /* pass mode: manual correction on top of the model, Hz */
    uint32_t riseS;       /* pass setup: seconds until rise            */
    uint16_t durS;        /* pass duration, s                          */
    int16_t  rit;         /* receiver incremental tuning, Hz           */
    uint8_t  mode, step, recede, unit_ms, en;
    uint8_t  emax, pf;    /* pass max elevation (deg), setup field 0..2 */
    uint8_t  ed[8];       /* typed digits                              */
} dop_t;

static const uint16_t dc_step[4] = { 1, 10, 100, 1000 };

/* Doppler shift in Hz, signed (+ approaching). vcms <= 9 999 900. */
static int32_t dc_delta(uint32_t f0, uint32_t vcms, uint8_t recede)
{
    const uint32_t ppb4 = (((vcms >> 12) * DC_M) >> 8) + (((vcms & 4095u) * DC_M) >> 20);
    const uint32_t a  = f0 / 1000000u;                  /* MHz            */
    const uint32_t bk = (f0 % 1000000u) / 1000u;        /* kHz remainder  */
    const uint32_t d  = (a * ppb4 + (bk * ppb4) / 1000u + 2000u) / 4000u;
    return recede ? -(int32_t)d : (int32_t)d;
}

static uint32_t dc_abs(int32_t v) { return v < 0 ? (uint32_t)(-v) : (uint32_t)v; }

/* Listening error: + means the receiver sits above the signal. */
static inline int32_t dc_err(const dop_t *s)
{
    return s->dial + s->rit - dc_delta(s->f0, s->vcms, s->recede);
}

/* Simulated signal bars 0..4 from the tuning error. */
static uint8_t dc_quality(int32_t err)
{
    const uint32_t e = dc_abs(err);
    return e <= 50 ? 4 : e <= 300 ? 3 : e <= 1000 ? 2 : e <= 3000 ? 1 : 0;
}

static int32_t dc_clamp(int32_t v, int32_t lim) { return v > lim ? lim : (v < -lim ? -lim : v); }

/* Velocity as shown, in the selected unit. */
static uint32_t dc_vshow(const dop_t *s)
{
    return s->unit_ms ? (s->vcms + 50u) / 100u : (s->vcms * 9u + 125u) / 250u;
}

static uint32_t dc_isqrt(uint32_t n)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > n) b >>= 2;
    while (b) {
        if (n >= r + b) { n -= r + b; r = (r >> 1) + b; } else r >>= 1;
        b >>= 2;
    }
    return r;
}

/* Radial speed of the satellite at `ageMs` after rise, cm/s; *recede = 1 after the closest approach.
 *
 * Shape: v = V u / sqrt(u^2 + c^2), u = 0 at the closest approach, 1 at the horizon (the pass is
 * stretched to durS), V = 7300 m/s. c depends on the maximum elevation (about 0.80 at 10 deg down
 * to 0.176 at 90 deg) and was fitted to the exact circular-orbit geometry (420 km altitude, Earth
 * rotation ignored): within 86 m/s of that model for 10..90 deg. */
static uint32_t dc_pass_vcms(uint8_t emax, uint32_t durS, uint32_t ageMs, uint8_t *recede)
{
    static const uint8_t cq[18] = { 210,154,122,101,87,76,68,62,58,54,51,49,47,46,45,45,44,44 };   /* c * 256 for 10, 15, ... 90 deg */
    const uint32_t dur = durS * 10u, t = ageMs / 100u;            /* deciseconds */
    const uint32_t x = (2u * t > dur) ? 2u * t - dur : dur - 2u * t;
    uint32_t u = x * 1024u / dur, c;
    uint8_t i, f;
    if (emax < 10u) emax = 10u;
    if (emax > 90u) emax = 90u;
    if (u > 1024u) u = 1024u;
    *recede = 2u * t > dur;
    i = (uint8_t)((emax - 10u) / 5u);
    f = (uint8_t)((emax - 10u) % 5u);
    c = ((uint32_t)cq[i] * (5u - f) + (uint32_t)cq[i + 1] * f) * 4u / 5u;   /* Q10 */
    return 2920000u * u / dc_isqrt((u * u + c * c) << 4);       /* 7300 m/s = 730000 cm/s; isqrt is x4, u is Q10 */
}

/* Pass mode: bring the dial to model + manual correction (force = even for a small move). */
static uint8_t dc_follow(dop_t *s, uint8_t force)
{
    const int32_t tgt = dc_clamp(dc_delta(s->f0, s->vcms, s->recede) + s->adj, DC_DIAL_MAX);
    const uint32_t d = dc_abs(tgt - s->dial);
    if (d == 0 || (!force && d < DC_THR)) return 0;
    s->dial = tgt;
    return R_TUNE | R_DRAW;
}

static uint8_t dc_set(dop_t *s, int32_t dial, int32_t rit)
{
    dial = dc_clamp(dial, DC_DIAL_MAX);
    rit  = dc_clamp(rit, DC_RIT_MAX);
    if (dial == s->dial && rit == s->rit) return 0;      /* clamped: nothing changes */
    s->dial = dial;
    s->rit  = (int16_t)rit;
    return R_TUNE | R_DRAW;
}

/* Hold acceleration: x1, x10 after 12 repeats, x100 after 40, capped. */
static int32_t dc_accel(int32_t base, uint8_t rep, int32_t cap)
{
    const int32_t v = base * (rep >= 40 ? 100 : rep >= 12 ? 10 : 1);
    return v > cap ? cap : v;
}

static uint32_t dc_typed(const dop_t *s)          /* typed digits as a number, right aligned */
{
    uint32_t v = 0;
    uint8_t i;
    for (i = 0; i < s->en; i++) v = v * 10u + s->ed[i];
    return v;
}

static uint32_t dc_hms(uint32_t v)                /* typed hhmmss -> seconds (mm, ss clamped to 59) */
{
    const uint32_t m = (v / 100u) % 100u, sec = v % 100u;
    return (v / 10000u) * 3600u + (m > 59u ? 59u : m) * 60u + (sec > 59u ? 59u : sec);
}

static uint8_t dc_commit(dop_t *s)
{
    uint8_t r = R_DRAW;
    uint32_t v = 0;
    uint8_t i;
    if (s->mode == DC_PSET) {
        v = dc_typed(s);
        if (s->en) {
            if (s->pf == 0) {
                s->riseS = dc_hms(v);
            } else if (s->pf == 1) {
                s->durS = (uint16_t)((v < 1u ? 1u : v) * 60u);
            } else {
                s->emax = (uint8_t)(v < 1u ? 1u : v > 90u ? 90u : v);
            }
        }
        s->en = 0;
        if (++s->pf == 3u) {
            s->mode = DC_PASS;
            s->adj = 0;
            return R_DRAW | R_START;
        }
        return r;
    }
    if (s->mode == DC_EDF) {
        if (s->en) {
            for (i = 0; i < 8; i++) v = v * 10 + (i < s->en ? s->ed[i] : 0);
            v *= 10;
            s->f0 = v < DC_F_MIN ? DC_F_MIN : (v > DC_F_MAX ? DC_F_MAX : v);
            s->dial = 0;
        }
        s->mode = DC_EDV;
    } else {
        if (s->en) {
            v = dc_typed(s);
            s->vcms = s->unit_ms ? v * 100u : (v * 250u + 4u) / 9u;
        }
        s->mode = DC_RUN;
        r |= R_TUNE;                                     /* f0 may have moved */
    }
    s->en = 0;
    return r;
}

/* One key press (or auto-repeat). dir = nav_dir(key): +1/-1 for the arrows.
 * While a pass runs the model drives the dial: arrows add a correction (adj) instead of moving it. */
static uint8_t dc_key(dop_t *s, uint8_t key, int8_t dir, uint8_t rep)
{
    const bool pass = s->mode == DC_PASS;
    if (!pass && s->mode != DC_RUN) {
        if (key <= 9u) {
            if (s->en < (s->mode == DC_EDF ? 8u : s->mode == DC_EDV ? 5u : s->pf ? 2u : 6u))
                s->ed[s->en++] = key;
            return R_DRAW;
        }
        if (key == 13u) {                                /* EXIT: backspace, then cancel */
            if (s->en) s->en--; else s->mode = DC_RUN;
            return R_DRAW;
        }
        return key == 10u ? dc_commit(s) : 0;            /* MENU */
    }
    if (dir) {
        const int32_t d = dir * dc_accel(dc_step[s->step], rep, 10000);
        if (!pass) return dc_set(s, s->dial + d, s->rit);
        s->adj = dc_clamp(s->adj + d, DC_DIAL_MAX);
        return dc_follow(s, 1) | R_DRAW;
    }
    switch (key) {
    case 1:  return dc_set(s, s->dial, s->rit - dc_accel(100, rep, 1000));
    case 3:  return dc_set(s, s->dial, s->rit + dc_accel(100, rep, 1000));
    case 2:  return dc_set(s, s->dial, 0);
    case 0:
        if (!pass) return dc_set(s, 0, 0);
        s->adj = 0;                                      /* pass: clear correction and RIT */
        return dc_set(s, s->dial, 0) | dc_follow(s, 1) | R_DRAW;
    case 14: s->step = (s->step + 1) & 3; return R_DRAW;                  /* STAR */
    case 7:  s->unit_ms ^= 1; return R_DRAW;
    case 8:  return pass ? R_PEAK : 0;                                    /* now = closest approach */
    case 13:
        if (!pass) return R_EXIT;
        s->mode = DC_RUN;                                                 /* EXIT: stop the pass */
        return R_STOP | R_DRAW;
    default: break;
    }
    if (pass) return 0;
    switch (key) {
    case 9:  return dc_set(s, dc_delta(s->f0, s->vcms, s->recede) - s->rit, s->rit);
    case 4:  s->mode = DC_PSET; s->pf = 0; s->en = 0; return R_DRAW;     /* pass setup  */
    case 15: s->recede ^= 1; return R_DRAW;                               /* F           */
    case 10: s->mode = DC_EDF; s->en = 0; return R_DRAW;                  /* MENU        */
    default: return 0;
    }
}

#endif
