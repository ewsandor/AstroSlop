#include "../../src/c/astro/astro.h"
#include "../../src/c/astro/events.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void near(double actual, double expected, double tolerance) {
  if (fabs(actual - expected) > tolerance) {
    fprintf(stderr, "expected %.9f, got %.9f (tolerance %.9f)\n",
            expected, actual, tolerance);
    assert(0);
  }
}

static bool fixed_position(void *context, double jd_utc, AstroEquatorial *position) {
  (void)jd_utc;
  *position = *(AstroEquatorial *)context;
  return true;
}

static void test_stellar_pointing(void) {
  // Astropy/ERFA apparent AltAz from FK5 J2000 coordinates, proper motion,
  // UT1=UTC and zero pressure. Geodetic coordinates are east-positive.
  static const struct {
    double ra, dec, pm_ra, pm_dec, jd, lat, lon, height, az, el;
  } references[] = {
    {101.2871, -16.7161, -553, -1205, 2461041.5, 51.4779, 0, 0, 179.053126, 21.765588},
    {101.2871, -16.7161, -553, -1205, 2464845.75, -30.169, -70.806, 2200, 195.675622, -41.614907},
    {101.2871, -16.7161, -553, -1205, 2461041.5, 69.65, 18.9553, 30, 197.266134, 2.659185},
    {219.8996, -60.8353, -3642, 699, 2461041.5, 51.4779, 0, 0, 130.168453, -56.475806},
    {219.8996, -60.8353, -3642, 699, 2464845.75, -30.169, -70.806, 2200, 211.546444, 45.624857},
    {219.8996, -60.8353, -3642, 699, 2464845.75, 69.65, 18.9553, 30, 276.721313, -71.205711},
    {279.2346, 38.7836, 202, 286, 2461041.5, 51.4779, 0, 0, 0.946190, 0.290882},
    {279.2346, 38.7836, 202, 286, 2464845.75, -30.169, -70.806, 2200, 8.337126, 20.379181},
    {279.2346, 38.7836, 202, 286, 2464845.75, 69.65, 18.9553, 30, 276.516096, 39.502922},
    {213.9154, 19.1825, -1093, -1998, 2461041.5, 51.4779, 0, 0, 60.079262, 1.149933},
    {213.9154, 19.1825, -1093, -1998, 2464845.75, -30.169, -70.806, 2200, 305.462132, 17.656690},
    {213.9154, 19.1825, -1093, -1998, 2461041.5, 69.65, 18.9553, 30, 78.923506, 16.236711},
  };
  double max_az = 0, max_el = 0;
  for (size_t i = 0; i < sizeof(references) / sizeof(references[0]); ++i) {
    AstroEquatorial position;
    AstroHorizontal horizontal;
    AstroObserver observer = {references[i].lat, references[i].lon, references[i].height};
    assert(astro_fixed_apparent(astro_julian_tt(references[i].jd), references[i].ra,
                               references[i].dec, references[i].pm_ra,
                               references[i].pm_dec, &position));
    assert(astro_horizontal(references[i].jd, &observer, &position, false, 10, &horizontal));
    assert(horizontal.azimuth_defined);
    double az_error = fabs(remainder(horizontal.azimuth_deg - references[i].az, 360));
    double el_error = fabs(horizontal.elevation_deg - references[i].el);
    if (az_error > 0.01 || el_error > 0.01) {
      fprintf(stderr, "ERFA star case %zu: az error %.6f, el error %.6f degrees\n",
              i, az_error, el_error);
      assert(0);
    }
    max_az = fmax(max_az, az_error);
    max_el = fmax(max_el, el_error);
  }
  printf("ERFA stellar pointing: %zu cases; max az %.5f, el %.5f degrees\n",
         sizeof(references) / sizeof(references[0]), max_az, max_el);
}

int main(void) {
  test_stellar_pointing();
  near(astro_julian_day(0), 2440587.5, 1e-10);
  const double utc_dates[] = {2457754.5, 2461041.5, 2461545.5};
  for (size_t i = 0; i < sizeof(utc_dates) / sizeof(utc_dates[0]); ++i) {
    near(astro_julian_tt(utc_dates[i]), utc_dates[i] + 69.184 / 86400.0, 1e-9);
  }
  // Meeus chapter 12, 1987 April 10 at 0h UT.
  near(astro_gmst(2446895.5) / ASTRO_DEG, 197.693195, 0.00001);

  const double jd = 2451545.0;
  AstroObserver equator = {0, 0, 0};
  AstroEquatorial position = {astro_local_sidereal(jd, 0) + ASTRO_PI / 2, 0, 0};
  AstroHorizontal horizontal;
  assert(astro_horizontal(jd, &equator, &position, false, 10, &horizontal));
  near(horizontal.azimuth_deg, 90, 1e-8);
  near(horizontal.elevation_deg, 0, 1e-8);
  assert(astro_horizontal(jd, &equator, &position, true, 10, &horizontal));
  near(horizontal.elevation_deg, 0.4847, 0.002);

  position.distance_au = 384400.0 / 149597870.7;
  assert(astro_horizontal(jd, &equator, &position, false, 10, &horizontal));
  near(horizontal.elevation_deg, -0.9507, 0.003);

  position.ra = astro_local_sidereal(jd, 0);
  position.distance_au = 0;
  assert(astro_horizontal(jd, &equator, &position, false, 10, &horizontal));
  near(horizontal.elevation_deg, 90, 1e-7);
  assert(!horizontal.azimuth_defined);
  equator.latitude_deg = 91;
  assert(!astro_horizontal(jd, &equator, &position, false, 10, &horizontal));

  AstroEquatorial star;
  star = (AstroEquatorial){2.3, 0.7, 0};
  astro_precess(2451545.0, 2461041.5, &star);
  astro_precess(2461041.5, 2451545.0, &star);
  near(star.ra, 2.3, 1e-10);
  near(star.dec, 0.7, 1e-10);
  assert(astro_fixed_apparent(2451545.0, 0, 90, 1000, -200, &star));
  assert(isfinite(star.ra) && isfinite(star.dec));
  assert(!astro_fixed_apparent(2451545.0, 360, 0, 0, 0, &star));

  equator.latitude_deg = 0;
  position = (AstroEquatorial){astro_local_sidereal(jd, 0), 0, 0};
  AstroEvent event;
  assert(astro_next_event(jd, &equator, fixed_position, &position, false, false, false, &event));
  assert(event.kind == ASTRO_EVENT_SET);
  near((event.jd_utc - jd) * 24, 6.021, 0.03);
  assert(astro_horizontal(event.jd_utc, &equator, &position, false, 10, &horizontal));
  near(horizontal.elevation_deg, -0.5667, 0.002);
  equator.latitude_deg = 80;
  position.dec = 80 * ASTRO_DEG;
  assert(astro_next_event(jd, &equator, fixed_position, &position, false, false, false, &event));
  assert(event.kind == ASTRO_EVENT_CIRCUMPOLAR);
  position.dec = -80 * ASTRO_DEG;
  assert(astro_next_event(jd, &equator, fixed_position, &position, false, false, false, &event));
  assert(event.kind == ASTRO_EVENT_NEVER_RISES);
  puts("astro core tests passed");
  return 0;
}
