#include "../../src/c/astro/ephemeris.h"
#include "../../src/c/astro/events.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  FILE *file;
  AstroEphemeris ephemeris;
} TestEphemeris;

typedef struct {
  double maximum;
  double sum_squared;
  unsigned count;
} ErrorStats;

static bool read_file(void *context, uint32_t offset, void *destination, size_t length) {
  FILE *file = context;
  return fseek(file, (long)offset, SEEK_SET) == 0 &&
         fread(destination, 1, length, file) == length;
}

static TestEphemeris open_ephemeris(const char *path) {
  TestEphemeris result = {.file = fopen(path, "rb")};
  assert(result.file);
  assert(astro_ephemeris_init(&result.ephemeris, read_file, result.file));
  return result;
}

static void record_error(ErrorStats *stats, double error) {
  error = fabs(error);
  if (error > stats->maximum) {
    stats->maximum = error;
  }
  stats->sum_squared += error * error;
  ++stats->count;
}

static double rms(const ErrorStats *stats) {
  return sqrt(stats->sum_squared / stats->count);
}

static void print_stats(const char *label, const char *unit, const ErrorStats *stats) {
  printf("%s: max %.9f %s, RMS %.9f %s\n", label, stats->maximum, unit,
         rms(stats), unit);
}

static double apparent_separation(AstroEquatorial a, AstroEquatorial b) {
  double dra = remainder(a.ra - b.ra, 2 * ASTRO_PI) * cos((a.dec + b.dec) / 2);
  return hypot(dra, a.dec - b.dec) / ASTRO_DEG;
}

static double clamp_unit(double value) {
  return fmax(-1, fmin(1, value));
}

static double illumination(AstroEquatorial moon, AstroEquatorial sun, double *phase_deg) {
  double cos_elongation = sin(moon.dec) * sin(sun.dec) +
    cos(moon.dec) * cos(sun.dec) * cos(moon.ra - sun.ra);
  double moon_distance = moon.distance_au;
  double sun_distance = sun.distance_au;
  double sun_moon_distance = sqrt(moon_distance * moon_distance +
    sun_distance * sun_distance - 2 * moon_distance * sun_distance * cos_elongation);
  double cos_phase = clamp_unit((moon_distance - sun_distance * cos_elongation) /
                                sun_moon_distance);
  *phase_deg = acos(cos_phase) / ASTRO_DEG;
  return (1 + cos_phase) / 2;
}

typedef struct {
  const AstroEphemeris *ephemeris;
} MoonContext;

static bool moon_position(void *context, double jd_utc, AstroEquatorial *position) {
  const MoonContext *moon = context;
  AstroBodyPosition body;
  if (!astro_ephemeris_position(moon->ephemeris, ASTRO_MOON, 0,
                                astro_julian_tt(jd_utc), &body)) {
    return false;
  }
  *position = body.equatorial;
  return true;
}

static void compare_events(const AstroEphemeris *truncated, const AstroEphemeris *full,
                           ErrorStats *seconds) {
  static const AstroObserver observers[] = {
    {51.4779, 0, 0},
    {-30.169, -70.806, 2200},
  };
  const double start = 2461041.5;
  const double end = 2465028.5;
  for (double jd = start; jd <= end; jd += 30) {
    for (size_t i = 0; i < sizeof(observers) / sizeof(observers[0]); ++i) {
      MoonContext short_context = {truncated};
      MoonContext full_context = {full};
      AstroEvent short_event, full_event;
      assert(astro_next_event(jd, &observers[i], moon_position, &short_context,
                              true, false, true, &short_event));
      assert(astro_next_event(jd, &observers[i], moon_position, &full_context,
                              true, false, true, &full_event));
      assert(short_event.kind == full_event.kind);
      if (short_event.kind == ASTRO_EVENT_RISE || short_event.kind == ASTRO_EVENT_SET) {
        record_error(seconds, (short_event.jd_utc - full_event.jd_utc) * 86400);
      }
    }
  }
}

int main(int argc, char **argv) {
  assert(argc == 3);
  TestEphemeris truncated = open_ephemeris(argv[1]);
  TestEphemeris full = open_ephemeris(argv[2]);
  ErrorStats longitude = {0}, latitude = {0}, distance = {0};
  ErrorStats apparent = {0}, altitude = {0}, azimuth = {0};
  ErrorStats phase = {0}, illumination_error = {0}, magnitude = {0}, rise_set = {0};

  // Daily samples span the documented 1900–2100 precision interval.
  for (double jd = 2415020.5; jd <= 2488069.5; jd += 1) {
    double short_lon, short_lat, short_distance;
    double full_lon, full_lat, full_distance;
    assert(astro_moon_ecliptic(&truncated.ephemeris, jd, &short_lon, &short_lat,
                               &short_distance));
    assert(astro_moon_ecliptic(&full.ephemeris, jd, &full_lon, &full_lat, &full_distance));
    record_error(&longitude, remainder(short_lon - full_lon, 2 * ASTRO_PI) / ASTRO_DEG);
    record_error(&latitude, (short_lat - full_lat) / ASTRO_DEG);
    record_error(&distance, short_distance - full_distance);
  }

  // Daily apparent, topocentric, phase and illumination comparisons over the
  // interval covered by the minor-body reference data.
  static const AstroObserver observers[] = {
    {51.4779, 0, 0},
    {-30.169, -70.806, 2200},
  };
  for (double jd = 2461041.5; jd <= 2465028.5; jd += 1) {
    AstroBodyPosition short_moon, full_moon, sun;
    assert(astro_ephemeris_position(&truncated.ephemeris, ASTRO_MOON, 0,
                                    astro_julian_tt(jd), &short_moon));
    assert(astro_ephemeris_position(&full.ephemeris, ASTRO_MOON, 0,
                                    astro_julian_tt(jd), &full_moon));
    assert(astro_ephemeris_position(&full.ephemeris, ASTRO_SUN, 0,
                                    astro_julian_tt(jd), &sun));
    record_error(&apparent, apparent_separation(short_moon.equatorial, full_moon.equatorial));
    record_error(&magnitude, short_moon.magnitude - full_moon.magnitude);
    double short_phase, full_phase;
    double short_illumination = illumination(short_moon.equatorial, sun.equatorial,
                                             &short_phase);
    double full_illumination = illumination(full_moon.equatorial, sun.equatorial,
                                            &full_phase);
    record_error(&phase, short_phase - full_phase);
    record_error(&illumination_error, short_illumination - full_illumination);
    for (size_t i = 0; i < sizeof(observers) / sizeof(observers[0]); ++i) {
      AstroHorizontal short_horizontal, full_horizontal;
      assert(astro_horizontal(jd, &observers[i], &short_moon.equatorial, false, 10,
                              &short_horizontal));
      assert(astro_horizontal(jd, &observers[i], &full_moon.equatorial, false, 10,
                              &full_horizontal));
      record_error(&altitude, short_horizontal.elevation_deg - full_horizontal.elevation_deg);
      if (short_horizontal.azimuth_defined && full_horizontal.azimuth_defined &&
          short_horizontal.elevation_deg < 89 && full_horizontal.elevation_deg < 89) {
        record_error(&azimuth, remainder(short_horizontal.azimuth_deg -
                                         full_horizontal.azimuth_deg, 360));
      }
    }
  }
  compare_events(&truncated.ephemeris, &full.ephemeris, &rise_set);

  print_stats("Ecliptic longitude", "degrees", &longitude);
  print_stats("Ecliptic latitude", "degrees", &latitude);
  print_stats("Distance", "km", &distance);
  print_stats("Apparent position", "degrees", &apparent);
  print_stats("Topocentric altitude", "degrees", &altitude);
  print_stats("Topocentric azimuth below 89-degree altitude", "degrees", &azimuth);
  print_stats("Phase angle", "degrees", &phase);
  print_stats("Illumination fraction", "fraction", &illumination_error);
  print_stats("Lunar magnitude", "mag", &magnitude);
  print_stats("Rise/set time", "seconds", &rise_set);

  assert(longitude.maximum < 0.0016);
  assert(latitude.maximum < 0.00081);
  assert(distance.maximum < 10);
  assert(apparent.maximum < 0.0019);
  assert(altitude.maximum < 0.002);
  assert(azimuth.maximum < 0.01);
  assert(phase.maximum < 0.002);
  assert(illumination_error.maximum < 0.00002);
  assert(magnitude.maximum < 0.001);
  assert(rise_set.maximum < 30);

  fclose(truncated.file);
  fclose(full.file);
  puts("Moon truncation comparisons passed");
  return 0;
}
