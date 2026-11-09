/* Host test of the pure logic.  gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
 *     -I. test/test_core.c -o /tmp/tc -lm && /tmp/tc                                            */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../doppler_core.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static dop_t S;
static void init(void) { memset(&S, 0, sizeof S); S.f0 = 145800000u; S.vcms = 700000u; S.unit_ms = 1; S.step = 2; S.durS = 600u; S.emax = 40; S.alt = 420; }
static int8_t dir_of(uint8_t k) { return k == 11 ? 1 : k == 12 ? -1 : 0; }     /* UV-K5 layout */
static uint8_t press(uint8_t k) { return dc_key(&S, k, dir_of(k), 0); }
static void digits(const char *d) { for (; *d; d++) press((uint8_t)(*d - '0')); }

int main(void)
{
    double worst = 0; long i;

    /* --- math --- */
    CHECK(dc_delta(145800000u, 0, 0) == 0);
    CHECK(dc_delta(145800000u, 700000u, 0) == 3404);
    CHECK(dc_delta(145800000u, 700000u, 1) == -3404);
    CHECK(dc_delta(437800000u, 766000u, 0) == 11186);
    srand(777);
    for (i = 0; i < 2000000; i++) {
        uint32_t f = 18000000u + (uint32_t)(((uint64_t)rand() * rand()) % 1282000000ull);
        uint32_t v = (uint32_t)(((uint64_t)rand() * rand()) % 9999901ull);
        long double ref = (long double)f * v / 29979245800.0L;
        double e = (double)fabsl((long double)dc_delta(f, v, 0) - ref);
        if (e > worst) worst = e;
        if (dc_delta(f, v, 1) != -dc_delta(f, v, 0)) { CHECK(!"asymmetric"); break; }
    }
    printf("worst |error| over 2M random cases (18 MHz..1.3 GHz, up to 99999 m/s): %.3f Hz\n", worst);
    CHECK(worst <= 1.5);
    { double w2 = 0; for (i = 0; i < 20000; i++) {                       /* ISS-class: 144-146, 435-438 MHz, <=8 km/s */
        uint32_t f = (i & 1 ? 435000000u : 144000000u) + (uint32_t)(rand() % 3000000);
        uint32_t v = (uint32_t)(rand() % 800001);
        double e = (double)fabsl((long double)dc_delta(f, v, 0) - (long double)f * v / 29979245800.0L);
        if (e > w2) w2 = e; }
      printf("worst |error| for ISS-class cases: %.3f Hz\n", w2); CHECK(w2 <= 1.0); }
    CHECK(dc_delta(1300000000u, 9999900u, 0) > 0);                       /* no overflow at the corner */

    /* --- quality / error --- */
    init(); CHECK(dc_quality(dc_err(&S)) == 0);
    press(9); CHECK(dc_err(&S) == 0 && dc_quality(0) == 4);
    CHECK(dc_quality(51) == 3 && dc_quality(-300) == 3 && dc_quality(1000) == 2 && dc_quality(3001) == 0);

    /* --- dial / RIT --- */
    init(); CHECK(press(11) == (R_TUNE | R_DRAW) && S.dial == 100);
    press(12); press(12); CHECK(S.dial == -100);
    press(3); press(3); CHECK(S.rit == 200); press(2); CHECK(S.rit == 0);
    press(0); CHECK(S.dial == 0);
    init(); press(3); press(3); press(9); CHECK(dc_err(&S) == 0);        /* auto-zero with RIT */
    { int k; init(); S.step = 0;                                         /* 1 Hz: 12*1 + 28*10 + 10*100 */
      for (k = 0; k < 50; k++) dc_key(&S, 11, 1, (uint8_t)k);
      CHECK(S.dial == 1292); }
    { int k; uint8_t r = 0; init(); S.step = 3;
      for (k = 0; k < 400; k++) dc_key(&S, 11, 1, 99);
      CHECK(S.dial == 400000);
      r = dc_key(&S, 11, 1, 99); CHECK(r == 0); }                        /* clamped: no retune, no redraw */
    { int k; init(); for (k = 0; k < 300; k++) { dc_key(&S, 3, 0, 99); }
      CHECK(S.rit == 5000);
      for (k = 0; k < 600; k++) { dc_key(&S, 1, 0, 99); }
      CHECK(S.rit == -5000); }

    /* --- toggles --- */
    init(); press(14); CHECK(S.step == 3); press(14); CHECK(S.step == 0);
    press(15); CHECK(S.recede == 1 && dc_delta(S.f0, S.vcms, S.recede) < 0);
    press(7); CHECK(S.unit_ms == 0 && S.vcms == 700000u && dc_vshow(&S) == 25200u);   /* 7000 m/s = 25200 km/h */
    press(7); CHECK(dc_vshow(&S) == 7000u);
    CHECK((press(13) & R_EXIT) != 0);
    CHECK(press(5) == 0 && press(6) == 0 && press(8) == 0 && press(16) == 0 && press(19) == 0);

    /* --- editing --- */
    init(); press(10); CHECK(S.mode == DC_EDF);
    digits("4358"); CHECK(S.en == 4);
    press(10); CHECK(S.mode == DC_EDV && S.f0 == 435800000u);
    digits("7660"); CHECK(S.en == 4);
    CHECK((press(10) & R_TUNE) && S.mode == DC_RUN && S.vcms == 766000u);
    CHECK(dc_delta(S.f0, S.vcms, 0) == dc_delta(435800000u, 766000u, 0));
    init(); press(10); digits("12"); press(13); CHECK(S.en == 1); press(13); CHECK(S.en == 0 && S.mode == DC_EDF);
    press(13); CHECK(S.mode == DC_RUN && S.f0 == 145800000u);
    init(); press(10); digits("99999999999"); CHECK(S.en == 8); press(10); CHECK(S.f0 == DC_F_MAX);
    digits("9999999"); CHECK(S.en == 5); press(10); CHECK(S.vcms == 9999900u);
    CHECK(labs((long)dc_delta(S.f0, S.vcms, 0) - 333561) <= 1);   /* exact 333560.76 */
    init(); press(10); digits("001"); press(10); CHECK(S.f0 == DC_F_MIN);
    init(); S.unit_ms = 0; press(10); press(10); digits("36"); press(10); CHECK(S.vcms == 1000u);   /* 36 km/h = 10 m/s */
    init(); press(10); press(10); press(10); CHECK(S.mode == DC_RUN && S.f0 == 145800000u && S.vcms == 700000u);
    /* arrows and toggles do nothing while editing */
    init(); press(10); CHECK(press(11) == 0 && press(15) == 0 && S.dial == 0 && S.recede == 0);


    /* --- pass mode: velocity curve vs an independent exact orbit model --- */
    {
        const long double R = 6371.0L, r = 6791.0L, w = sqrtl(398600.4418L / (r * r * r));
        double worstv = 0; int n;
        srand(99);
        for (n = 0; n < 200000; n++) {
            int E = 10 + rand() % 81;
            uint32_t durS = 60u * (uint32_t)(5 + rand() % 10);
            uint32_t age = (uint32_t)(((uint64_t)rand() * rand()) % (durS * 1000ull + 1));
            uint8_t rec; uint32_t got = dc_pass_vcms((uint8_t)E, 420, durS, age, &rec);
            /* exact: beta from tan(E) = (cos b - R/r)/sin b by bisection */
            long double lo = 0, hi = acosl(R / r), b, phi, u, a, rho, ex;
            int it;
            for (it = 0; it < 60; it++) { b = (lo + hi) / 2;
                if ((cosl(b) - R / r) / sinl(b) > tanl(E * 3.14159265358979L / 180)) lo = b; else hi = b; }
            b = (lo + hi) / 2; phi = acosl((R / r) / cosl(b));
            u = fabsl(2.0L * age - durS * 1000.0L) / (durS * 1000.0L);
            a = u * phi;
            rho = sqrtl(R * R + r * r - 2 * R * r * cosl(b) * cosl(a));
            ex = R * r * w * cosl(b) * sinl(a) / rho * 1000.0L;                 /* m/s */
            { double e = fabs((double)got / 100.0 - (double)ex); if (e > worstv) worstv = e; }
            { long long dm = 2ll * age - (long long)durS * 1000;                          /* ignore +/-200 ms around the zero crossing */
              if ((dm > 200 || dm < -200) && rec != (dm > 0)) { CHECK(!"recede flag"); break; } }
        }
        printf("pass curve: worst |table - exact model| = %.0f m/s (%.0f Hz at 145.8 MHz, %.0f Hz at 437.8 MHz)\n",
               worstv, worstv * 145.8e6 / 299792458, worstv * 437.8e6 / 299792458);
        CHECK(worstv < 100.0);
        { uint8_t rc; uint32_t v0 = dc_pass_vcms(35, 420, 660, 0, &rc), vm, ve;
          CHECK(rc == 0 && v0 > 690000u && v0 < 710000u);                           /* ~6.98 km/s at rise */
          vm = dc_pass_vcms(35, 420, 660, 330000, &rc); CHECK(vm < 3000u);               /* ~0 at the closest approach */
          ve = dc_pass_vcms(35, 420, 660, 660000, &rc); CHECK(rc == 1 && ve > 690000u && ve < 710000u);
          CHECK(dc_pass_vcms(5, 420, 600, 100000, &rc) == dc_pass_vcms(10, 420, 600, 100000, &rc));   /* below 10 deg uses the 10 deg row */
          CHECK(dc_pass_vcms(99, 420, 600, 100000, &rc) == dc_pass_vcms(90, 420, 600, 100000, &rc)); }
    }

    /* --- altitude: curve vs the exact model at other orbits --- */
    {
        const long double R = 6371.0L;
        double w_all = 0, w_iss = 0, w_ign = 0; int n;
        srand(4242);
        for (n = 0; n < 100000; n++) {
            int E = 10 + rand() % 81;
            int alt = 350 + rand() % 851;                                      /* 350..1200 km */
            uint32_t durS = 600u;
            uint32_t age = (uint32_t)(rand() % 600001);
            uint8_t rec;
            long double r = R + alt, w = sqrtl(398600.4418L / (r * r * r)), lo = 0, hi = acosl(R / r), b, phi, u, a, rho, ex;
            double e, e0; int it;
            for (it = 0; it < 50; it++) { b = (lo + hi) / 2;
                if ((cosl(b) - R / r) / sinl(b) > tanl(E * 3.14159265358979L / 180)) lo = b; else hi = b; }
            b = (lo + hi) / 2; phi = acosl((R / r) / cosl(b));
            u = fabsl(2.0L * age - durS * 1000.0L) / (durS * 1000.0L);
            a = u * phi; rho = sqrtl(R * R + r * r - 2 * R * r * cosl(b) * cosl(a));
            ex = R * r * w * cosl(b) * sinl(a) / rho * 1000.0L;
            e  = fabs((double)dc_pass_vcms((uint8_t)E, (uint16_t)alt, durS, age, &rec) / 100.0 - (double)ex);
            e0 = fabs((double)dc_pass_vcms((uint8_t)E, 420, durS, age, &rec) / 100.0 - (double)ex);
            if (e > w_all) w_all = e;
            if (alt >= 400 && alt <= 430 && e > w_iss) w_iss = e;
            if (e0 > w_ign) w_ign = e0;
        }
        printf("altitude 350..1200 km: worst error %.0f m/s (%.0f Hz at 145.8 MHz, %.0f Hz at 437.8 MHz); ignoring altitude: %.0f m/s\n",
               w_all, w_all * 145.8e6 / 299792458, w_all * 437.8e6 / 299792458, w_ign);
        CHECK(w_all < 260.0);
        CHECK(w_ign > 3.0 * w_all);                                             /* the correction is worth it */
    }

    /* --- pass setup typing: your example, rise in 5:20:45, 11 min, max 35 deg --- */
    init(); press(4); CHECK(S.mode == DC_PSET && S.pf == 0);
    digits("52045"); CHECK(S.en == 5);
    press(10); CHECK(S.riseS == 19245u && S.pf == 1 && S.mode == DC_PSET);
    digits("11"); press(10); CHECK(S.durS == 660 && S.pf == 2);
    digits("35"); press(10); CHECK(S.pf == 3 && S.mode == DC_PSET);
    digits("550"); CHECK((press(10) & R_START) && S.mode == DC_PASS && S.emax == 35 && S.alt == 550 && S.adj == 0);
    init(); press(4); digits("9999999"); CHECK(S.en == 6); press(10); CHECK(S.riseS == 99u * 3600u + 59u * 60u + 59u);
    digits("00"); press(10); CHECK(S.durS == 60);                                   /* 0 min -> 1 min */
    digits("99"); press(10); CHECK(S.emax == 90);
    digits("5"); { uint8_t rr = press(10); CHECK((rr & R_START) && S.alt == 200); }                /* 5 km -> 200 km */
    init(); press(4); press(10); press(10); press(10); press(10);                   /* MENU x4 keeps the defaults */
    CHECK(S.mode == DC_PASS && S.alt == 420);
    init(); press(4); press(10); press(10); press(10); digits("99999"); CHECK(S.en == 4); press(10); CHECK(S.alt == 2000);
    init(); press(4); digits("1"); press(13); CHECK(S.en == 0); press(13); CHECK(S.mode == DC_RUN);
    init(); press(4); CHECK(press(11) == 0 && S.dial == 0);                         /* arrows do nothing while typing */

    /* --- pass mode: follow, corrections, timeline keys, stop --- */
    {   uint8_t rc, r;
        init(); S.durS = 660; S.emax = 35; press(4); press(10); press(10); press(10); press(10);
        CHECK(S.mode == DC_PASS);
        S.vcms = dc_pass_vcms(S.emax, S.alt, S.durS, 0, &rc); S.recede = rc;
        r = dc_follow(&S, 0); CHECK(r == (R_TUNE | R_DRAW) && S.dial == dc_delta(S.f0, S.vcms, S.recede));
        CHECK(dc_follow(&S, 0) == 0);                                              /* already there */
        S.vcms += 2000;                                                            /* ~20 m/s = ~10 Hz: below threshold */
        CHECK(dc_follow(&S, 0) == 0);
        S.vcms += 40000;                                                           /* 400 m/s = ~194 Hz */
        r = dc_follow(&S, 0); if (!r) { S.vcms += 5000; r = dc_follow(&S, 0); } CHECK(r & R_TUNE);
        { int32_t d0 = dc_delta(S.f0, S.vcms, S.recede);
          r = dc_key(&S, 11, 1, 0); CHECK((r & R_TUNE) && S.adj == 100 && S.dial == d0 + 100);   /* arrow = +100 Hz correction */
          press(0); CHECK(S.adj == 0 && S.dial == d0); }
        r = dc_key(&S, 3, 0, 0); CHECK((r & R_TUNE) && S.rit == 100);               /* RIT independent of the dial */
        r = dc_key(&S, 1, 0, 0); CHECK((r & R_TUNE) && S.rit == 0);
        CHECK(dc_key(&S, 8, 0, 0) == R_PEAK);
        CHECK(dc_key(&S, 15, 0, 0) == 0 && dc_key(&S, 10, 0, 0) == 0 && dc_key(&S, 9, 0, 0) == 0);   /* F, MENU, 9 ignored */
        r = dc_key(&S, 13, 0, 0); CHECK((r & R_STOP) && S.mode == DC_RUN);
        CHECK((dc_key(&S, 13, 0, 0) & R_EXIT));                                    /* second EXIT leaves the app */
    }

    /* --- whole pass as the app drives it: your example, 145.800 MHz, 11 min, max 35 deg --- */
    {   uint8_t rec; uint32_t age; int retunes = 0, steps = 0; long worst = 0;
        init(); S.durS = 660; S.emax = 35; S.mode = DC_PASS;
        puts("pass at 145.800 MHz, 11 min, 35 deg (minute: velocity, shift):");
        for (age = 0; age <= 660000u; age += 500) {
            S.vcms = dc_pass_vcms(S.emax, S.alt, S.durS, age, &rec); S.recede = rec;
            if (dc_follow(&S, 0) & R_TUNE) retunes++;
            { long e = labs((long)S.dial - dc_delta(S.f0, S.vcms, S.recede)); if (e > worst) worst = e; }
            steps++;
            if (age % 60000u == 0) printf("  %2u min  %s%5.2f km/s  dF %+6d Hz  RX %+6d Hz\n", age / 60000u,
                   rec ? "-" : "+", S.vcms / 100000.0, dc_delta(S.f0, S.vcms, S.recede), S.dial);
        }
        printf("  retunes: %d in %d s (each = 40 ms audio gap), worst dial lag %ld Hz\n", retunes, steps / 2, worst);
        CHECK(retunes >= 25 && retunes <= 60 && worst < 260);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails != 0;
}
