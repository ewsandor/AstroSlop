#include "astro.h"

#include <math.h>
#include <stddef.h>

#define TWO_PI (2.0 * ASTRO_PI)
#define ARCSEC (ASTRO_DEG / 3600.0)
#define AU_KM 149597870.7

double astro_wrap(double angle) {
  double result = angle - TWO_PI * floor(angle / TWO_PI);
  return result >= TWO_PI ? 0 : (result < 0 ? result + TWO_PI : result);
}

double astro_asin(double value) {
  return atan2(value, sqrt((1.0 - value) * (1.0 + value)));
}

double astro_acos(double value) {
  return atan2(sqrt((1.0 - value) * (1.0 + value)), value);
}

double astro_tan(double angle) {
  return sin(angle) / cos(angle);
}

#define wrap astro_wrap

static double clamp_unit(double value) {
  return fmax(-1.0, fmin(1.0, value));
}

static void from_vector(double x, double y, double z, AstroEquatorial *position) {
  position->ra = wrap(atan2(y, x));
  position->dec = atan2(z, sqrt(x * x + y * y));
}

double astro_julian_day(int64_t unix_seconds) {
  return 2440587.5 + (double)unix_seconds / 86400.0;
}

static double historical_delta_t(double jd) {
  const double year = 2000.0 + (jd - 2451545.0) / 365.2425;
  double t;
  if (year < 1800.0) {
    t = (year - 1820.0) / 100.0;
    return -20.0 + 32.0 * t * t;
  }
  if (year < 1860.0) {
    t = year - 1800.0;
    return 13.72 + t * (-0.332447 + t * (0.0068612 +
           t * (0.0041116 + t * (-0.00037436 + t * (0.0000121272 +
           t * (-0.0000001699 + t * 0.000000000875))))));
  }
  if (year < 1900.0) {
    t = year - 1860.0;
    return 7.62 + t * (0.5737 + t * (-0.251754 +
           t * (0.01680668 + t * (-0.0004473624 + t / 233174.0))));
  }
  if (year < 1920.0) {
    t = year - 1900.0;
    return -2.79 + t * (1.494119 + t * (-0.0598939 +
           t * (0.0061966 - 0.000197 * t)));
  }
  if (year < 1941.0) {
    t = year - 1920.0;
    return 21.20 + t * (0.84493 + t * (-0.076100 + t * 0.0020936));
  }
  if (year < 1961.0) {
    t = year - 1950.0;
    return 29.07 + 0.407 * t - t * t / 233.0 + t * t * t / 2547.0;
  }
  t = year - 1975.0;
  return 45.45 + 1.067 * t - t * t / 260.0 - t * t * t / 718.0;
}

double astro_julian_tt(double jd_utc) {
  // IERS Bulletin C leap seconds; Espenak and Meeus polynomial before 1972.
  static const struct {
    double jd;
    int8_t tai_utc;
  } leaps[] = {
    {2441317.5, 10}, {2441499.5, 11}, {2441683.5, 12}, {2442048.5, 13},
    {2442413.5, 14}, {2442778.5, 15}, {2443144.5, 16}, {2443509.5, 17},
    {2443874.5, 18}, {2444239.5, 19}, {2444786.5, 20}, {2445151.5, 21},
    {2445516.5, 22}, {2446247.5, 23}, {2447161.5, 24}, {2447892.5, 25},
    {2448257.5, 26}, {2448804.5, 27}, {2449169.5, 28}, {2449534.5, 29},
    {2450083.5, 30}, {2450630.5, 31}, {2451179.5, 32}, {2453736.5, 33},
    {2454832.5, 34}, {2456109.5, 35}, {2457204.5, 36}, {2457754.5, 37},
  };
  if (jd_utc < leaps[0].jd) {
    return jd_utc + historical_delta_t(jd_utc) / 86400.0;
  }
  double delta = 0;
  for (size_t i = 0; i < sizeof(leaps) / sizeof(leaps[0]); ++i) {
    if (jd_utc < leaps[i].jd) {
      break;
    }
    delta = leaps[i].tai_utc + 32.184;
  }
  return jd_utc + delta / 86400.0;
}

double astro_gmst(double jd_utc) {
  // Meeus, Astronomical Algorithms, equation 12.4.
  double t = (jd_utc - 2451545.0) / 36525.0;
  return wrap((280.46061837 + 360.98564736629 * (jd_utc - 2451545.0) +
               t * t * (0.000387933 - t / 38710000.0)) * ASTRO_DEG);
}

double astro_mean_obliquity(double jd_tt) {
  double t = (jd_tt - 2451545.0) / 36525.0;
  return (23.0 + 26.0 / 60.0 + 21.448 / 3600.0) * ASTRO_DEG -
         (46.8150 * t + 0.00059 * t * t -
          0.001813 * t * t * t) * ARCSEC;
}

#if !defined(ASTRO_LOW_MEMORY)
void astro_nutation(double jd_tt, double *longitude, double *obliquity,
                    double *mean_obliquity) {
  // Meeus chapter 22, four-term approximation.
  double t = (jd_tt - 2451545.0) / 36525.0;
  double omega = (125.04452 - 1934.136261 * t + 0.0020708 * t * t +
                  t * t * t / 450000.0) * ASTRO_DEG;
  double sun = (280.4665 + 36000.7698 * t) * ASTRO_DEG;
  double moon = (218.3165 + 481267.8813 * t) * ASTRO_DEG;
  *longitude = (-17.20 * sin(omega) - 1.32 * sin(2 * sun) -
                0.23 * sin(2 * moon) + 0.21 * sin(2 * omega)) * ARCSEC;
  *obliquity = (9.20 * cos(omega) + 0.57 * cos(2 * sun) +
                0.10 * cos(2 * moon) - 0.09 * cos(2 * omega)) * ARCSEC;
  *mean_obliquity = astro_mean_obliquity(jd_tt);
}
#endif

double astro_local_sidereal(double jd_utc, double longitude_deg) {
  double dpsi, deps, epsilon;
  astro_nutation(astro_julian_tt(jd_utc), &dpsi, &deps, &epsilon);
  return wrap(astro_gmst(jd_utc) + dpsi * cos(epsilon + deps) +
              longitude_deg * ASTRO_DEG);
}

void astro_precess(double from_jd_tt, double to_jd_tt, AstroEquatorial *position) {
  // Meeus chapter 21, equations 21.2-21.4.
  double base = (from_jd_tt - 2451545.0) / 36525.0;
  double t = (to_jd_tt - from_jd_tt) / 36525.0;
  double zeta = ((2306.2181 + 1.39656 * base - 0.000139 * base * base) * t +
                 (0.30188 - 0.000344 * base) * t * t + 0.017998 * t * t * t) * ARCSEC;
  double z = ((2306.2181 + 1.39656 * base - 0.000139 * base * base) * t +
              (1.09468 + 0.000066 * base) * t * t + 0.018203 * t * t * t) * ARCSEC;
  double theta = ((2004.3109 - 0.85330 * base - 0.000217 * base * base) * t -
                  (0.42665 + 0.000217 * base) * t * t - 0.041833 * t * t * t) * ARCSEC;
  double a = cos(position->dec) * sin(position->ra + zeta);
  double b = cos(theta) * cos(position->dec) * cos(position->ra + zeta) -
             sin(theta) * sin(position->dec);
  double c = sin(theta) * cos(position->dec) * cos(position->ra + zeta) +
             cos(theta) * sin(position->dec);
  position->ra = wrap(atan2(a, b) + z);
  position->dec = astro_asin(clamp_unit(c));
}

void astro_precess_j2000(double jd_tt, AstroEquatorial *position) {
  astro_precess(2451545.0, jd_tt, position);
}

#if !defined(ASTRO_LOW_MEMORY)
static void apply_nutation(double jd_tt, AstroEquatorial *position) {
  double dpsi, deps, epsilon;
  astro_nutation(jd_tt, &dpsi, &deps, &epsilon);
  double x = cos(position->dec) * cos(position->ra);
  double y = cos(position->dec) * sin(position->ra);
  double z = sin(position->dec);
  double ecliptic_y = cos(epsilon) * y + sin(epsilon) * z;
  double ecliptic_z = -sin(epsilon) * y + cos(epsilon) * z;
  double rotated_x = cos(dpsi) * x - sin(dpsi) * ecliptic_y;
  double rotated_y = sin(dpsi) * x + cos(dpsi) * ecliptic_y;
  double true_epsilon = epsilon + deps;
  from_vector(rotated_x, cos(true_epsilon) * rotated_y - sin(true_epsilon) * ecliptic_z,
              sin(true_epsilon) * rotated_y + cos(true_epsilon) * ecliptic_z, position);
}

static void apply_annual_aberration(double jd_tt, AstroEquatorial *position) {
  // Meeus chapter 23, equation 23.3, applied in the tangent plane.
  double t = (jd_tt - 2451545.0) / 36525.0;
  double mean_longitude = (280.46646 + 36000.76983 * t + 0.0003032 * t * t) * ASTRO_DEG;
  double mean_anomaly = (357.52911 + 35999.05029 * t - 0.0001537 * t * t) * ASTRO_DEG;
  double sun = mean_longitude +
    ((1.914602 - 0.004817 * t - 0.000014 * t * t) * sin(mean_anomaly) +
     (0.019993 - 0.000101 * t) * sin(2 * mean_anomaly) +
     0.000289 * sin(3 * mean_anomaly)) * ASTRO_DEG;
  double e = 0.016708634 - 0.000042037 * t - 0.0000001267 * t * t;
  double peri = (102.93735 + 1.71946 * t + 0.00046 * t * t) * ASTRO_DEG;
  double dpsi, deps, epsilon;
  astro_nutation(jd_tt, &dpsi, &deps, &epsilon);
  double sa = sin(position->ra), ca = cos(position->ra);
  double sd = sin(position->dec), cd = cos(position->dec);
  double q1 = ca * cos(epsilon);
  double q2 = sin(epsilon) * cd - cos(epsilon) * sa * sd;
  double q3 = ca * sd;
  double dra_cos_dec = 20.49552 * ARCSEC *
    (e * (q1 * cos(peri) + sa * sin(peri)) - q1 * cos(sun) - sa * sin(sun));
  double ddec = 20.49552 * ARCSEC *
    (e * (cos(peri) * q2 + sin(peri) * q3) - cos(sun) * q2 - sin(sun) * q3);
  from_vector(cd * ca - dra_cos_dec * sa - ddec * sd * ca,
              cd * sa + dra_cos_dec * ca - ddec * sd * sa,
              sd + ddec * cd, position);
}

void astro_apply_apparent_corrections(double jd_tt, AstroEquatorial *position) {
  apply_annual_aberration(jd_tt, position);
  apply_nutation(jd_tt, position);
}

void astro_apply_ecliptic_aberration(double jd_tt, double *longitude, double *latitude,
                                     double distance_au, bool sun) {
  if (sun) {
    *longitude -= 20.4898 * ARCSEC / distance_au;
    return;
  }
  // Meeus chapter 23, equation 23.2.
  double t = (jd_tt - 2451545.0) / 36525.0;
  double m = (357.52911 + 35999.05029 * t - 0.0001537 * t * t) * ASTRO_DEG;
  double sun_longitude = (280.46646 + 36000.76983 * t + 0.0003032 * t * t +
                          (1.914602 - 0.004817 * t - 0.000014 * t * t) * sin(m) +
                          (0.019993 - 0.000101 * t) * sin(2 * m) +
                          0.000289 * sin(3 * m)) * ASTRO_DEG;
  double e = 0.016708634 - 0.000042037 * t - 0.0000001267 * t * t;
  double peri = (102.93735 + 1.71946 * t + 0.00046 * t * t) * ASTRO_DEG;
  double lambda = *longitude, beta = *latitude;
  *longitude += 20.49552 * ARCSEC *
    (e * cos(peri - lambda) - cos(sun_longitude - lambda)) / cos(beta);
  *latitude -= 20.49552 * ARCSEC * sin(beta) *
    (sin(sun_longitude - lambda) - e * sin(peri - lambda));
}
#endif

bool astro_fixed_apparent(double jd_tt, double ra_deg, double dec_deg,
                          double pm_ra_mas, double pm_dec_mas,
                          AstroEquatorial *position) {
  if (!position || !isfinite(jd_tt) || !isfinite(ra_deg) ||
      !isfinite(dec_deg) || !isfinite(pm_ra_mas) || !isfinite(pm_dec_mas) ||
      ra_deg < 0 || ra_deg >= 360 || dec_deg < -90 || dec_deg > 90) {
    return false;
  }
  double ra = ra_deg * ASTRO_DEG, dec = dec_deg * ASTRO_DEG;
  double years = (jd_tt - 2451545.0) / 365.25;
  double da = pm_ra_mas * years * ARCSEC / 1000.0;
  double dd = pm_dec_mas * years * ARCSEC / 1000.0;
  double sa = sin(ra), ca = cos(ra), sd = sin(dec), cd = cos(dec);
  from_vector(cd * ca - da * sa - dd * sd * ca,
              cd * sa + da * ca - dd * sd * sa,
              sd + dd * cd, position);
  position->distance_au = 0;
  astro_precess_j2000(jd_tt, position);
  astro_apply_apparent_corrections(jd_tt, position);
  return true;
}

bool astro_horizontal(double jd_utc, const AstroObserver *observer,
                      const AstroEquatorial *position, bool refraction,
                      double temperature_c, AstroHorizontal *horizontal) {
  if (!observer || !position || !horizontal || !isfinite(jd_utc) ||
      !isfinite(observer->latitude_deg) || !isfinite(observer->longitude_deg) ||
      !isfinite(observer->elevation_m) || !isfinite(position->ra) ||
      !isfinite(position->dec) || !isfinite(position->distance_au) ||
      !isfinite(temperature_c) || observer->latitude_deg < -90 ||
      observer->latitude_deg > 90 || observer->longitude_deg < -180 ||
      observer->longitude_deg > 180 || observer->elevation_m < -500 ||
      observer->elevation_m > 12000 || position->dec < -ASTRO_PI / 2 ||
      position->dec > ASTRO_PI / 2 || position->distance_au < 0 ||
      temperature_c <= -273.0) {
    return false;
  }
  double phi = observer->latitude_deg * ASTRO_DEG;
  double hour_angle = astro_local_sidereal(jd_utc, observer->longitude_deg) - position->ra;
  double dec = position->dec;
  if (position->distance_au > 0) {
    // Meeus chapters 11 and 40, ellipsoidal topocentric correction.
    double u = atan2(0.99664719 * sin(phi), cos(phi));
    double height_ratio = observer->elevation_m / 6378140.0;
    double rho_sin = 0.99664719 * sin(u) + height_ratio * sin(phi);
    double rho_cos = cos(u) + height_ratio * cos(phi);
    double sin_parallax = (6378.14 / AU_KM) / position->distance_au;
    if (sin_parallax >= 1.0) {
      return false;
    }
    double delta_ra = atan2(-rho_cos * sin_parallax * sin(hour_angle),
                            cos(dec) - rho_cos * sin_parallax * cos(hour_angle));
    dec = atan2((sin(dec) - rho_sin * sin_parallax) * cos(delta_ra),
                cos(dec) - rho_cos * sin_parallax * cos(hour_angle));
    hour_angle -= delta_ra;
  }
  double altitude = astro_asin(clamp_unit(sin(phi) * sin(dec) +
                                         cos(phi) * cos(dec) * cos(hour_angle)));
  double azimuth = wrap(atan2(-sin(hour_angle) * cos(dec),
                              sin(dec) * cos(phi) -
                              cos(dec) * sin(phi) * cos(hour_angle)));
  horizontal->azimuth_defined = fabs(altitude / ASTRO_DEG) <= 89.99;
  horizontal->azimuth_deg = azimuth / ASTRO_DEG;
  horizontal->elevation_deg = altitude / ASTRO_DEG;
  if (refraction && horizontal->elevation_deg > -1.0) {
    // Saemundsson/Meeus chapter 16, pressure from the standard atmosphere.
    double h = horizontal->elevation_deg;
    double pressure = 1013.25 * exp(5.25588 * log(1.0 - 2.25577e-5 * observer->elevation_m));
    double correction = 1.02 / astro_tan((h + 10.3 / (h + 5.11)) * ASTRO_DEG);
    horizontal->elevation_deg += correction / 60.0 * (pressure / 1010.0) *
                                 (283.0 / (273.0 + temperature_c));
  }
  return true;
}
