#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ASTRO_PI 3.14159265358979323846
#define ASTRO_DEG (ASTRO_PI / 180.0)

typedef struct {
  double ra;
  double dec;
  double distance_au;  // Zero for effectively infinite distance.
} AstroEquatorial;

typedef struct {
  double latitude_deg;
  double longitude_deg;
  double elevation_m;
} AstroObserver;

typedef struct {
  double azimuth_deg;
  double elevation_deg;
  bool azimuth_defined;
} AstroHorizontal;

double astro_julian_day(int64_t unix_seconds);
double astro_wrap(double angle);
double astro_asin(double value);
double astro_acos(double value);
double astro_tan(double angle);
double astro_julian_tt(double jd_utc);
double astro_gmst(double jd_utc);
double astro_local_sidereal(double jd_utc, double longitude_deg);
double astro_mean_obliquity(double jd_tt);
#if defined(ASTRO_LOW_MEMORY)
static inline void astro_nutation(double jd_tt, double *longitude, double *obliquity,
                                  double *mean_obliquity) {
  *longitude = 0;
  *obliquity = 0;
  *mean_obliquity = astro_mean_obliquity(jd_tt);
}
#else
void astro_nutation(double jd_tt, double *longitude, double *obliquity,
                    double *mean_obliquity);
#endif
void astro_precess(double from_jd_tt, double to_jd_tt, AstroEquatorial *position);
void astro_precess_j2000(double jd_tt, AstroEquatorial *position);
#if defined(ASTRO_LOW_MEMORY)
static inline void astro_apply_apparent_corrections(double jd_tt,
                                                     AstroEquatorial *position) {
  (void)jd_tt;
  (void)position;
}
static inline void astro_apply_ecliptic_aberration(double jd_tt, double *longitude,
                                                    double *latitude, double distance_au,
                                                    bool sun) {
  (void)jd_tt;
  (void)longitude;
  (void)latitude;
  (void)distance_au;
  (void)sun;
}
#else
void astro_apply_apparent_corrections(double jd_tt, AstroEquatorial *position);
void astro_apply_ecliptic_aberration(double jd_tt, double *longitude, double *latitude,
                                     double distance_au, bool sun);
#endif
bool astro_fixed_apparent(double jd_tt, double ra_deg, double dec_deg,
                          double pm_ra_mas, double pm_dec_mas,
                          AstroEquatorial *position);
bool astro_horizontal(double jd_utc, const AstroObserver *observer,
                      const AstroEquatorial *position, bool refraction,
                      double temperature_c, AstroHorizontal *horizontal);
