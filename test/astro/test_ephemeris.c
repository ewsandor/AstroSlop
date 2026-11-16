#include "../../src/c/astro/ephemeris.h"
#include "../../src/c/astro/events.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static bool read_file(void *context, uint32_t offset, void *destination, size_t length) {
  FILE *file = context;
  return fseek(file, (long)offset, SEEK_SET) == 0 &&
         fread(destination, 1, length, file) == length;
}

static void near(const char *label, double actual, double expected, double tolerance) {
  if (fabs(actual - expected) > tolerance) {
    fprintf(stderr, "%s: expected %.8f, got %.8f (tolerance %.8f)\n",
            label, expected, actual, tolerance);
    abort();
  }
}

static bool sun_position(void *context, double jd_utc, AstroEquatorial *position) {
  AstroBodyPosition body;
  if (!astro_ephemeris_position(context, ASTRO_SUN, 0, astro_julian_tt(jd_utc), &body)) {
    return false;
  }
  *position = body.equatorial;
  return true;
}

typedef struct {
  AstroEphemeris *ephemeris;
  AstroBodyKind kind;
  unsigned index;
} BodyContext;

static bool body_position(void *context, double jd_utc, AstroEquatorial *position) {
  const BodyContext *body = context;
  AstroBodyPosition result;
  if (!astro_ephemeris_position(body->ephemeris, body->kind, body->index,
                                astro_julian_tt(jd_utc), &result)) return false;
  *position = result.equatorial;
  return true;
}

int main(int argc, char **argv) {
  assert(argc == 2);
  FILE *file = fopen(argv[1], "rb");
  assert(file);
  AstroEphemeris ephemeris;
  assert(astro_ephemeris_init(&ephemeris, read_file, file));

  // Meeus example 47.a, 1992 April 12 (JDE).
  double lon, lat, distance;
  assert(astro_moon_ecliptic(&ephemeris, 2448724.5, &lon, &lat, &distance));
  near("Moon longitude", lon / ASTRO_DEG, 133.162655, 0.000002);
  near("Moon latitude", lat / ASTRO_DEG, -3.229126, 0.000002);
  near("Moon distance", distance, 368409.7, 0.1);

  // NASA/JPL Horizons, Earth geocenter, 2026-01-01 00:00 UTC, apparent RA/Dec.
  static const struct {
    AstroBodyKind kind;
    unsigned index;
    double ra, dec, distance;
    const char *name;
  } references[] = {
    {ASTRO_SUN, 0, 281.49470, -23.01725, 0.98332666, "Sun"},
    {ASTRO_MOON, 0, 63.92031, 26.40370, 0.00241344, "Moon"},
    {ASTRO_PLANET, 3, 283.87961, -23.72001, 2.41069877, "Mars"},
    {ASTRO_PLANET, 4, 113.12433, 21.97914, 4.24266571, "Jupiter"},
    {ASTRO_PLUTO, 0, 305.93605, -23.21992, 36.32806611, "Pluto"},
    {ASTRO_MINOR, 0, 296.45776, -22.64844, 3.14418131, "Vesta"},
  };
  double jde = astro_julian_tt(2461041.5);
  for (size_t i = 0; i < sizeof(references) / sizeof(references[0]); ++i) {
    AstroBodyPosition position;
    assert(astro_ephemeris_position(&ephemeris, references[i].kind, references[i].index,
                                    jde, &position));
    printf("%s: %.6f %.6f %.8f mag %.2f\n", references[i].name,
           position.equatorial.ra / ASTRO_DEG, position.equatorial.dec / ASTRO_DEG,
           position.equatorial.distance_au, position.magnitude);
    near(references[i].name, position.equatorial.ra / ASTRO_DEG, references[i].ra, 0.005);
    near(references[i].name, position.equatorial.dec / ASTRO_DEG, references[i].dec, 0.005);
    near(references[i].name, position.equatorial.distance_au, references[i].distance, 0.0002);
    assert(position.magnitude_known);
  }
  // Horizons apparent geocentric RA/Dec, sampled throughout the element table.
  static const struct {
    AstroBodyKind kind;
    unsigned index;
    double jd, ra, dec;
    const char *name;
    double max_arcseconds;
  } accuracy[] = {
    {ASTRO_MINOR, 0, 2461041.5, 296.457761064, -22.648436127, "Vesta 2026", 10},
    {ASTRO_MINOR, 0, 2461222.5,  19.331717668,   0.764054084, "Vesta 2026 Jul", 10},
    {ASTRO_MINOR, 0, 2462683.5,  45.901998837,  11.235319088, "Vesta 2030", 10},
    {ASTRO_MINOR, 0, 2465028.5, 289.561110043, -24.091175787, "Vesta 2036", 10},
    {ASTRO_MINOR, 1, 2461041.5, 270.873608644, -13.528611326, "Juno 2026", 10},
    {ASTRO_MINOR, 1, 2461222.5, 307.568244844,  -3.442582485, "Juno 2026 Jul", 10},
    {ASTRO_MINOR, 1, 2462683.5, 273.115910590,  -5.050589871, "Juno 2030", 10},
    {ASTRO_MINOR, 1, 2465028.5, 157.212360588,   0.190482550, "Juno 2036", 10},
    {ASTRO_MINOR, 2, 2461041.5,  10.008166257,  -5.357051620, "Ceres 2026", 10},
    {ASTRO_MINOR, 2, 2461222.5,  72.351892773,  20.139675705, "Ceres 2026 Jul", 10},
    {ASTRO_MINOR, 2, 2462683.5,  38.069203126,   6.214483672, "Ceres 2030", 10},
    {ASTRO_MINOR, 2, 2465028.5, 192.675786259,   4.688704431, "Ceres 2036", 10},
    {ASTRO_MINOR, 3, 2461041.5, 321.494588576,  -3.927486487, "Pallas 2026", 10},
    {ASTRO_MINOR, 3, 2461222.5,  17.642142721,   3.724909867, "Pallas 2026 Jul", 10},
    {ASTRO_MINOR, 3, 2462683.5, 339.830617318,  11.949134166, "Pallas 2030", 10},
    {ASTRO_MINOR, 3, 2465028.5, 139.030598619, -22.867245568, "Pallas 2036", 10},
    {ASTRO_PLANET, 5, 2461041.5, 357.380959207, -3.596394732, "Saturn 2026", 3},
    {ASTRO_PLANET, 5, 2462683.5,  62.007466091, 19.062597703, "Saturn 2030", 3},
    {ASTRO_PLANET, 5, 2465028.5, 155.208831070, 11.765348931, "Saturn 2036", 3},
    {ASTRO_MOON, 0, 2461041.5,  63.920306258,  26.403701421, "Moon 2026", 10},
    {ASTRO_MOON, 0, 2462683.5, 101.096101226,  20.893254330, "Moon 2030", 10},
    {ASTRO_MOON, 0, 2465028.5,  36.124120279,   8.982650278, "Moon 2036", 10},
  };
  for (size_t i = 0; i < sizeof(accuracy) / sizeof(accuracy[0]); ++i) {
    AstroBodyPosition position;
    assert(astro_ephemeris_position(&ephemeris, accuracy[i].kind, accuracy[i].index,
                                    astro_julian_tt(accuracy[i].jd), &position));
    double dra = remainder(position.equatorial.ra / ASTRO_DEG - accuracy[i].ra, 360) *
                 cos(accuracy[i].dec * ASTRO_DEG);
    double ddec = position.equatorial.dec / ASTRO_DEG - accuracy[i].dec;
    double arcseconds = hypot(dra, ddec) * 3600;
    printf("%s error: %.2f arcsec\n", accuracy[i].name, arcseconds);
    near(accuracy[i].name, arcseconds, 0, accuracy[i].max_arcseconds);
    assert(!position.outside_minor_epochs);
  }
  static const struct { double jd, magnitude; } saturn_magnitudes[] = {
    {2461041.5, 1.007}, {2461222.5, 0.775},
    {2462683.5, 0.194}, {2465028.5, 0.517},
  };
  for (size_t i = 0; i < sizeof(saturn_magnitudes) / sizeof(saturn_magnitudes[0]); ++i) {
    AstroBodyPosition position;
    assert(astro_ephemeris_position(&ephemeris, ASTRO_PLANET, 5,
                                    astro_julian_tt(saturn_magnitudes[i].jd), &position));
    printf("Saturn magnitude %.2f (Horizons %.2f)\n",
           position.magnitude, saturn_magnitudes[i].magnitude);
    near("Saturn magnitude", position.magnitude, saturn_magnitudes[i].magnitude, 0.3);
  }

  // Near the end of midnight sun at 67.5 N, the Sun is initially circumpolar
  // but sets approximately 35.7 hours later (Astropy/ERFA, standard horizon).
  AstroObserver polar = {67.5, 0, 0};
  AstroEvent event;
  assert(astro_next_event(2461235.0, &polar, sun_position, &ephemeris,
                          true, true, false, &event));
  assert(event.kind == ASTRO_EVENT_SET);
  near("Polar sunset hours", (event.jd_utc - 2461235.0) * 24, 35.7, 0.1);
  // A 20-minute sunrise at the end of polar night must not be skipped.
  assert(astro_next_event(2461400.5, &polar, sun_position, &ephemeris,
                          true, true, false, &event));
  assert(event.kind == ASTRO_EVENT_RISE);
  near("Polar sunrise hours", (event.jd_utc - 2461400.5) * 24, 35.83, 0.1);
  // The same brief event between three-hour samples exercises the extremum search.
  assert(astro_next_event(2461400.5625, &polar, sun_position, &ephemeris,
                          true, true, false, &event));
  assert(event.kind == ASTRO_EVENT_RISE);
  near("Between-sample sunrise hours", (event.jd_utc - 2461400.5625) * 24, 34.33, 0.1);
  // Also catch a short excursion in the first search interval.
  assert(astro_next_event(2461401.958333333, &polar, sun_position, &ephemeris,
                          true, true, false, &event));
  assert(event.kind == ASTRO_EVENT_RISE);
  near("First-interval sunrise hours", (event.jd_utc - 2461401.958333333) * 24, 0.83, 0.1);
  assert(astro_next_event(2461400.0, &polar, sun_position, &ephemeris,
                          true, true, false, &event));
  assert(event.kind == ASTRO_EVENT_RISE);
  near("48-hour sunrise hours", (event.jd_utc - 2461400.0) * 24, 47.83, 0.1);
  assert(astro_next_event(2461232.0, &polar, sun_position, &ephemeris,
                          true, true, false, &event));
  assert(event.kind == ASTRO_EVENT_NONE);

  // The watch spreads seven ephemeris samples over separate event-loop turns.
  // Compare their interpolation and event result with direct calculations.
  static const struct {
    AstroBodyKind kind;
    unsigned index;
    double jd;
  } interpolated_bodies[] = {
    {ASTRO_SUN, 0, 2461400.5},
    {ASTRO_MOON, 0, 2461041.5},
    {ASTRO_MOON, 0, 2461400.5},
    {ASTRO_PLANET, 0, 2461041.5},
    {ASTRO_MINOR, 0, 2461222.5},
  };
  for (size_t i = 0; i < sizeof(interpolated_bodies) / sizeof(interpolated_bodies[0]); ++i) {
    BodyContext body = {&ephemeris, interpolated_bodies[i].kind, interpolated_bodies[i].index};
    AstroEventPositions samples = {.start_jd_utc = interpolated_bodies[i].jd};
    for (unsigned sample = 0; sample < ASTRO_EVENT_SAMPLES; ++sample) {
      assert(body_position(&body, samples.start_jd_utc + sample * ASTRO_EVENT_SAMPLE_DAYS,
                           &samples.positions[sample]));
    }
    for (unsigned hour = 0; hour <= 48; ++hour) {
      double jd = samples.start_jd_utc + hour / 24.0;
      AstroEquatorial exact, approximate;
      assert(body_position(&body, jd, &exact));
      assert(astro_event_interpolate(&samples, jd, &approximate));
      double error = hypot(remainder(exact.ra - approximate.ra, 2 * ASTRO_PI) *
                           cos(exact.dec), exact.dec - approximate.dec) / ASTRO_DEG;
      near("Interpolated apparent position, degrees", error, 0, 0.001);
    }
    AstroEvent exact, approximate;
    bool sun = body.kind == ASTRO_SUN, moon = body.kind == ASTRO_MOON;
    assert(astro_next_event(samples.start_jd_utc, &polar, body_position, &body,
                            true, sun, moon, &exact));
    assert(astro_next_event(samples.start_jd_utc, &polar, astro_event_interpolate, &samples,
                            true, sun, moon, &approximate));
    assert(exact.kind == approximate.kind);
    if (exact.kind == ASTRO_EVENT_RISE || exact.kind == ASTRO_EVENT_SET) {
      near("Interpolated event, seconds",
           (approximate.jd_utc - exact.jd_utc) * 86400, 0, 30);
    }
    AstroEquatorial invalid_position;
    assert(!astro_event_interpolate(&samples, samples.start_jd_utc - 0.01, &invalid_position));
    assert(!astro_event_interpolate(&samples, samples.start_jd_utc + 2.01, &invalid_position));
  }
  AstroEventPositions wrapping = {.start_jd_utc = 2461041.5};
  for (unsigned i = 0; i < ASTRO_EVENT_SAMPLES; ++i) {
    wrapping.positions[i] = (AstroEquatorial){
      fmod((359 + 2 * i) * ASTRO_DEG, 2 * ASTRO_PI), 0, 1
    };
  }
  AstroEquatorial across_zero;
  assert(astro_event_interpolate(&wrapping, wrapping.start_jd_utc + 1.0 / 6.0, &across_zero));
  near("Interpolated RA wraps through zero", remainder(across_zero.ra, 2 * ASTRO_PI), 0, 1e-8);

  AstroBodyPosition invalid;
  assert(!astro_ephemeris_position(&ephemeris, ASTRO_PLUTO, 0, 2488069.5, &invalid));
  assert(!astro_ephemeris_position(&ephemeris, ASTRO_PLANET, 2, jde, &invalid));
  fclose(file);
  puts("ephemeris tests passed");
  return 0;
}
