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

typedef struct {
  AstroEphemeris *ephemeris;
  AstroBodyKind kind;
  unsigned index;
  bool fixed;
  double ra_deg;
  double dec_deg;
} PositionContext;

static bool position_at(void *context, double jd_utc, AstroEquatorial *position) {
  const PositionContext *body = context;
  if (body->fixed) {
    return astro_fixed_apparent(astro_julian_tt(jd_utc), body->ra_deg, body->dec_deg,
                                0, 0, position);
  }
  AstroBodyPosition result;
  if (!astro_ephemeris_position(body->ephemeris, body->kind, body->index,
                                astro_julian_tt(jd_utc), &result)) {
    return false;
  }
  *position = result.equatorial;
  return true;
}

static void emit_position(const char *category, unsigned kind, unsigned index,
                          unsigned date, AstroEquatorial position) {
  printf("P %s %u %u %u %.12f %.12f\n", category, kind, index, date,
         position.ra / ASTRO_DEG, position.dec / ASTRO_DEG);
}

static void emit_event(const char *name, PositionContext *body, double jd_utc,
                       AstroObserver observer, bool sun, bool moon) {
  AstroEvent event;
  if (!astro_next_event(jd_utc, &observer, position_at, body, true, sun, moon, &event)) {
    fprintf(stderr, "failed to calculate event %s\n", name);
    abort();
  }
  printf("E %s %u %.12f\n", name, (unsigned)event.kind, event.jd_utc);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  FILE *file = fopen(argv[1], "rb");
  assert(file);
  AstroEphemeris ephemeris;
  assert(astro_ephemeris_init(&ephemeris, read_file, file));
#if defined(ASTRO_LOW_MEMORY)
  puts("C low");
#else
  puts("C full");
#endif

  static const double dates[] = {2451545.0, 2461041.5, 2462683.5, 2465028.5};
  static const struct {
    AstroBodyKind kind;
    unsigned index;
    const char *category;
  } bodies[] = {
    {ASTRO_SUN, 0, "sun"}, {ASTRO_MOON, 0, "moon"},
    {ASTRO_PLANET, 0, "planet"}, {ASTRO_PLANET, 1, "planet"},
    {ASTRO_PLANET, 3, "planet"}, {ASTRO_PLANET, 4, "planet"},
    {ASTRO_PLANET, 5, "planet"}, {ASTRO_PLANET, 6, "planet"},
    {ASTRO_PLANET, 7, "planet"}, {ASTRO_PLUTO, 0, "pluto"},
    {ASTRO_MINOR, 0, "minor"}, {ASTRO_MINOR, 1, "minor"},
    {ASTRO_MINOR, 2, "minor"}, {ASTRO_MINOR, 3, "minor"},
  };
  for (unsigned date = 0; date < sizeof(dates) / sizeof(dates[0]); ++date) {
    double dpsi, deps, epsilon;
    astro_nutation(astro_julian_tt(dates[date]), &dpsi, &deps, &epsilon);
    printf("M %u %.12f %.12f %.12f\n", date, dpsi, deps, epsilon);
    for (size_t i = 0; i < sizeof(bodies) / sizeof(bodies[0]); ++i) {
      if (bodies[i].kind == ASTRO_MINOR && date == 0) continue;
      AstroBodyPosition result;
      if (!astro_ephemeris_position(&ephemeris, bodies[i].kind, bodies[i].index,
                                    astro_julian_tt(dates[date]), &result)) {
        if (bodies[i].kind == ASTRO_MINOR) continue;
        fprintf(stderr, "failed position kind %u index %u date %u\n",
                (unsigned)bodies[i].kind, bodies[i].index, date);
        abort();
      }
      emit_position(bodies[i].category, bodies[i].kind, bodies[i].index, date,
                    result.equatorial);
    }

    for (unsigned ra = 0; ra < 360; ra += 15) {
      for (int dec = -90; dec <= 90; dec += 15) {
        AstroEquatorial star;
        assert(astro_fixed_apparent(astro_julian_tt(dates[date]), ra, dec, 0, 0, &star));
        emit_position("star", ra, (unsigned)(dec + 90), date, star);
      }
    }
  }

  AstroObserver ordinary = {40.7, -74.0, 10};
  PositionContext body = {&ephemeris, ASTRO_SUN, 0, false, 0, 0};
  emit_event("sun-midlatitude", &body, dates[1], ordinary, true, false);
  body.kind = ASTRO_MOON;
  emit_event("moon-midlatitude", &body, dates[1], ordinary, false, true);
  body.kind = ASTRO_PLANET;
  body.index = 3;
  emit_event("mars-midlatitude", &body, dates[1], ordinary, false, false);
  body.fixed = true;
  body.ra_deg = 180;
  body.dec_deg = 20;
  emit_event("star-midlatitude", &body, dates[1], ordinary, false, false);

  AstroObserver polar = {67.5, 0, 0};
  body.fixed = false;
  body.kind = ASTRO_SUN;
  body.index = 0;
  emit_event("sun-polar-set", &body, 2461235.0, polar, true, false);
  emit_event("sun-polar-rise", &body, 2461400.5, polar, true, false);
  fclose(file);
  return 0;
}
