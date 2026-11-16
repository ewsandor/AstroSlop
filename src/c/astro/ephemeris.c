#include "ephemeris.h"

#include <math.h>
#include <string.h>

#define ARCSEC (ASTRO_DEG / 3600.0)
#define TWO_PI (2.0 * ASTRO_PI)
#define AU_KM 149597870.7
#define LIGHT_DAYS_PER_AU 0.0057755183
#define FORMAT_VERSION 2u
#define HEADER_SIZE 44u
#define DIRECTORY_ENTRY_SIZE 8u
#define TERM_SIZE 24u
#define ELEMENT_SIZE 56u
#define MOON_LONGITUDE_SIZE 12u
#define MOON_LATITUDE_SIZE 8u
#define PLUTO_TERM_SIZE 52u
#define READ_TERMS 4u  // Limit stack use on 64 KB color watches.

typedef struct {
  double x, y, z;
} Vector;

typedef struct {
  double longitude, latitude, radius;
} Spherical;

#define wrap astro_wrap

static double clamp_unit(double value) {
  return fmax(-1.0, fmin(1.0, value));
}

static uint16_t u16(const uint8_t *p) {
  return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static double f64(const uint8_t *p) {
  uint64_t bits = 0;
  for (unsigned i = 0; i < 8; ++i) {
    bits |= (uint64_t)p[i] << (8 * i);
  }
  double value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

bool astro_ephemeris_init(AstroEphemeris *ephemeris, AstroResourceRead read,
                          void *context) {
  uint8_t header[HEADER_SIZE];
  if (!ephemeris || !read || !read(context, 0, header, sizeof(header)) ||
      memcmp(header, "ASEP", 4) != 0 || header[4] != FORMAT_VERSION || header[5] != 8 ||
      header[6] != 3 || header[7] != 6 || u16(header + 8) != 4 ||
      u16(header + 10) < 2 || u16(header + 12) == 0 ||
      u32(header + 24) < HEADER_SIZE || u32(header + 28) < u32(header + 24) ||
      u32(header + 32) < u32(header + 28) || u16(header + 36) == 0 ||
      u16(header + 38) == 0 || u16(header + 40) == 0) {
    return false;
  }
  ephemeris->read = read;
  ephemeris->context = context;
  ephemeris->epoch_count = u16(header + 10);
  ephemeris->epoch_step = u16(header + 12);
  ephemeris->first_epoch = f64(header + 16);
  ephemeris->directory_offset = u32(header + 24);
  ephemeris->minor_offset = u32(header + 28);
  ephemeris->meeus_offset = u32(header + 32);
  ephemeris->moon_longitude_count = u16(header + 36);
  ephemeris->moon_latitude_count = u16(header + 38);
  ephemeris->pluto_count = u16(header + 40);
  return isfinite(ephemeris->first_epoch);
}

static bool vsop(const AstroEphemeris *ephemeris, unsigned planet, double jd_tt,
                 Spherical *result) {
  if (!ephemeris || !ephemeris->read || planet >= 8 || !isfinite(jd_tt)) {
    return false;
  }
  double tau = (jd_tt - 2451545.0) / 365250.0;
  double coordinates[3] = {0, 0, 0};
  uint8_t entry[DIRECTORY_ENTRY_SIZE];
  uint8_t terms[READ_TERMS * TERM_SIZE];
  for (unsigned axis = 0; axis < 3; ++axis) {
    double tau_power = 1;
    for (unsigned power = 0; power < 6; ++power) {
      uint32_t entry_offset = ephemeris->directory_offset +
        ((planet * 3u + axis) * 6u + power) * DIRECTORY_ENTRY_SIZE;
      if (!ephemeris->read(ephemeris->context, entry_offset, entry, sizeof(entry))) {
        return false;
      }
      uint32_t term_offset = u32(entry);
      unsigned count = u16(entry + 4);
      if (term_offset > ephemeris->minor_offset ||
          count > (ephemeris->minor_offset - term_offset) / TERM_SIZE) {
        return false;
      }
      double sum = 0;
      for (unsigned done = 0; done < count;) {
        unsigned batch = count - done < READ_TERMS ? count - done : READ_TERMS;
        size_t bytes = batch * TERM_SIZE;
        if (!ephemeris->read(ephemeris->context, term_offset + done * TERM_SIZE,
                             terms, bytes)) {
          return false;
        }
        for (unsigned i = 0; i < batch; ++i) {
          const uint8_t *term = terms + i * TERM_SIZE;
          sum += f64(term) * cos(f64(term + 8) + f64(term + 16) * tau);
        }
        done += batch;
      }
      coordinates[axis] += sum * tau_power;
      tau_power *= tau;
    }
  }
  result->longitude = wrap(coordinates[0]);
  result->latitude = coordinates[1];
  result->radius = coordinates[2];
  return isfinite(result->radius) && result->radius > 0;
}

static Vector spherical_vector(Spherical sphere) {
  double xy = sphere.radius * cos(sphere.latitude);
  return (Vector){xy * cos(sphere.longitude), xy * sin(sphere.longitude),
                  sphere.radius * sin(sphere.latitude)};
}

static Vector subtract(Vector a, Vector b) {
  return (Vector){a.x - b.x, a.y - b.y, a.z - b.z};
}

static double length(Vector v) {
  return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

static Spherical vector_spherical(Vector v) {
  return (Spherical){wrap(atan2(v.y, v.x)), atan2(v.z, sqrt(v.x * v.x + v.y * v.y)), length(v)};
}

static AstroEquatorial ecliptic_equatorial(Spherical sphere, double obliquity) {
  Vector ecliptic = spherical_vector(sphere);
  double y = ecliptic.y * cos(obliquity) - ecliptic.z * sin(obliquity);
  double z = ecliptic.y * sin(obliquity) + ecliptic.z * cos(obliquity);
  return (AstroEquatorial){wrap(atan2(y, ecliptic.x)),
                           atan2(z, sqrt(ecliptic.x * ecliptic.x + y * y)), sphere.radius};
}

static Vector equatorial_ecliptic(AstroEquatorial position, double obliquity) {
  double cd = cos(position.dec);
  double x = position.distance_au * cd * cos(position.ra);
  double y = position.distance_au * cd * sin(position.ra);
  double z = position.distance_au * sin(position.dec);
  return (Vector){x, y * cos(obliquity) + z * sin(obliquity),
                  -y * sin(obliquity) + z * cos(obliquity)};
}

static Vector earth_j2000(Spherical earth, double jd_tt) {
  double dpsi, deps, epsilon;
  astro_nutation(jd_tt, &dpsi, &deps, &epsilon);
  AstroEquatorial equatorial = ecliptic_equatorial(earth, epsilon);
  astro_precess(jd_tt, 2451545.0, &equatorial);
  return equatorial_ecliptic(equatorial, 23.4392911111 * ASTRO_DEG);
}

static void ecliptic_fk5(Spherical *position, double jd_tt) {
  // Meeus chapter 32, equation 32.3.
  double t = (jd_tt - 2451545.0) / 36525.0;
  double lp = position->longitude - (1.397 * t + 0.00031 * t * t) * ASTRO_DEG;
  position->longitude += (-0.09033 + 0.03916 * (cos(lp) + sin(lp)) *
                          astro_tan(position->latitude)) * ARCSEC;
  position->latitude += 0.03916 * (cos(lp) - sin(lp)) * ARCSEC;
}

static void ecliptic_aberration(Spherical *position, double jd_tt) {
  // Meeus chapter 23, equation 23.2.
  double t = (jd_tt - 2451545.0) / 36525.0;
  double m = (357.52911 + 35999.05029 * t - 0.0001537 * t * t) * ASTRO_DEG;
  double sun = (280.46646 + 36000.76983 * t + 0.0003032 * t * t +
                (1.914602 - 0.004817 * t - 0.000014 * t * t) * sin(m) +
                (0.019993 - 0.000101 * t) * sin(2 * m) + 0.000289 * sin(3 * m)) *
               ASTRO_DEG;
  double e = 0.016708634 - 0.000042037 * t - 0.0000001267 * t * t;
  double peri = (102.93735 + 1.71946 * t + 0.00046 * t * t) * ASTRO_DEG;
  double lambda = position->longitude, beta = position->latitude;
  position->longitude += 20.49552 * ARCSEC *
    (e * cos(peri - lambda) - cos(sun - lambda)) / cos(beta);
  position->latitude -= 20.49552 * ARCSEC * sin(beta) *
    (sin(sun - lambda) - e * sin(peri - lambda));
}

static AstroEquatorial apparent_ecliptic(Spherical sphere, double jd_tt) {
  double dpsi, deps, epsilon;
  astro_nutation(jd_tt, &dpsi, &deps, &epsilon);
  sphere.longitude += dpsi;
  return ecliptic_equatorial(sphere, epsilon + deps);
}

static double moon_eccentricity_factor(int8_t m, double e) {
  return m == 0 ? 1 : (m == 1 || m == -1 ? e : e * e);
}

bool astro_moon_ecliptic(const AstroEphemeris *ephemeris, double jd_tt, double *longitude,
                         double *latitude, double *distance_km) {
  // Meeus chapter 47; tables 47.A/47.B are streamed from the ephemeris resource
  // (coefficients via Sonia Keys' MIT-licensed meeus package).
  if (!ephemeris || !ephemeris->read || !isfinite(jd_tt)) {
    return false;
  }
  double t = (jd_tt - 2451545.0) / 36525.0;
  double t2 = t * t, t3 = t2 * t, t4 = t3 * t;
  double lp = (218.3164477 + 481267.88123421 * t - 0.0015786 * t2 +
               t3 / 538841.0 - t4 / 65194000.0) * ASTRO_DEG;
  double d = (297.8501921 + 445267.1114034 * t - 0.0018819 * t2 +
              t3 / 545868.0 - t4 / 113065000.0) * ASTRO_DEG;
  double m = (357.5291092 + 35999.0502909 * t - 0.0001535 * t2 +
              t3 / 24490000.0) * ASTRO_DEG;
  double mp = (134.9633964 + 477198.8675055 * t + 0.0087414 * t2 +
               t3 / 69699.0 - t4 / 14712000.0) * ASTRO_DEG;
  double f = (93.272095 + 483202.0175233 * t - 0.0036539 * t2 -
              t3 / 3526000.0 + t4 / 863310000.0) * ASTRO_DEG;
  double a1 = (119.75 + 131.849 * t) * ASTRO_DEG;
  double a2 = (53.09 + 479264.29 * t) * ASTRO_DEG;
  double a3 = (313.45 + 481266.484 * t) * ASTRO_DEG;
  double e = 1 - 0.002516 * t - 0.0000074 * t2;
  double sum_l = 3958 * sin(a1) + 1962 * sin(lp - f) + 318 * sin(a2);
  double sum_r = 0;
  double sum_b = -2235 * sin(lp) + 382 * sin(a3) + 175 * sin(a1 - f) +
                 175 * sin(a1 + f) + 127 * sin(lp - mp) - 115 * sin(lp + mp);
  uint8_t terms[READ_TERMS * MOON_LONGITUDE_SIZE];
  uint32_t offset = ephemeris->meeus_offset;
  for (unsigned done = 0; done < ephemeris->moon_longitude_count;) {
    unsigned batch = ephemeris->moon_longitude_count - done;
    batch = batch < READ_TERMS ? batch : READ_TERMS;
    if (!ephemeris->read(ephemeris->context, offset, terms, batch * MOON_LONGITUDE_SIZE)) {
      return false;
    }
    for (unsigned i = 0; i < batch; ++i) {
      const uint8_t *term = terms + i * MOON_LONGITUDE_SIZE;
      int8_t tm = (int8_t)term[1];
      double factor = moon_eccentricity_factor(tm, e);
      double angle = (int8_t)term[0] * d + tm * m + (int8_t)term[2] * mp + (int8_t)term[3] * f;
      sum_l += factor * (int32_t)u32(term + 4) * sin(angle);
      sum_r += factor * (int32_t)u32(term + 8) * cos(angle);
    }
    offset += batch * MOON_LONGITUDE_SIZE;
    done += batch;
  }
  for (unsigned done = 0; done < ephemeris->moon_latitude_count;) {
    unsigned batch = ephemeris->moon_latitude_count - done;
    batch = batch < READ_TERMS ? batch : READ_TERMS;
    if (!ephemeris->read(ephemeris->context, offset, terms, batch * MOON_LATITUDE_SIZE)) {
      return false;
    }
    for (unsigned i = 0; i < batch; ++i) {
      const uint8_t *term = terms + i * MOON_LATITUDE_SIZE;
      int8_t tm = (int8_t)term[1];
      double angle = (int8_t)term[0] * d + tm * m + (int8_t)term[2] * mp + (int8_t)term[3] * f;
      sum_b += moon_eccentricity_factor(tm, e) * (int32_t)u32(term + 4) * sin(angle);
    }
    offset += batch * MOON_LATITUDE_SIZE;
    done += batch;
  }
  *longitude = wrap(lp + sum_l * 1e-6 * ASTRO_DEG);
  *latitude = sum_b * 1e-6 * ASTRO_DEG;
  *distance_km = 385000.56 + sum_r * 1e-3;
  return isfinite(*longitude) && isfinite(*latitude) && isfinite(*distance_km);
}

static bool pluto_heliocentric(const AstroEphemeris *ephemeris, double jd_tt,
                               Spherical *result) {
  // Meeus chapter 37; table 37.A is streamed from the ephemeris resource
  // (coefficients via Sonia Keys' MIT-licensed meeus package).
  double t = (jd_tt - 2451545.0) / 36525.0;
  double j = (34.35 + 3034.9057 * t) * ASTRO_DEG;
  double s = (50.08 + 1222.1138 * t) * ASTRO_DEG;
  double p = (238.96 + 144.96 * t) * ASTRO_DEG;
  double longitude = 238.958116 + 144.96 * t;
  double latitude = -3.908239;
  double radius = 40.7241346;
  uint8_t terms[READ_TERMS * PLUTO_TERM_SIZE];
  uint32_t offset = ephemeris->meeus_offset +
    ephemeris->moon_longitude_count * MOON_LONGITUDE_SIZE +
    ephemeris->moon_latitude_count * MOON_LATITUDE_SIZE;
  for (unsigned done = 0; done < ephemeris->pluto_count;) {
    unsigned batch = ephemeris->pluto_count - done;
    batch = batch < READ_TERMS ? batch : READ_TERMS;
    if (!ephemeris->read(ephemeris->context, offset, terms, batch * PLUTO_TERM_SIZE)) {
      return false;
    }
    for (unsigned i = 0; i < batch; ++i) {
      const uint8_t *term = terms + i * PLUTO_TERM_SIZE;
      double angle = (int8_t)term[0] * j + (int8_t)term[1] * s + (int8_t)term[2] * p;
      double sa = sin(angle), ca = cos(angle);
      longitude += f64(term + 4) * sa + f64(term + 12) * ca;
      latitude += f64(term + 20) * sa + f64(term + 28) * ca;
      radius += f64(term + 36) * sa + f64(term + 44) * ca;
    }
    offset += batch * PLUTO_TERM_SIZE;
    done += batch;
  }
  *result = (Spherical){wrap(longitude * ASTRO_DEG), latitude * ASTRO_DEG, radius};
  return isfinite(radius) && radius > 0;
}

static bool minor_elements(const AstroEphemeris *ephemeris, unsigned body, double jd_tt,
                           double elements[7], bool *outside) {
  if (body >= 4) {
    return false;
  }
  double fractional = (jd_tt - ephemeris->first_epoch) / ephemeris->epoch_step;
  *outside = fractional < 0 || fractional > ephemeris->epoch_count - 1;
  long index = (long)floor(fractional + 0.5);
  if (index < 0) {
    index = 0;
  } else if (index >= ephemeris->epoch_count) {
    index = ephemeris->epoch_count - 1;
  }
  uint32_t offset = ephemeris->minor_offset +
    (body * ephemeris->epoch_count + (uint32_t)index) * ELEMENT_SIZE;
  uint8_t bytes[ELEMENT_SIZE];
  if (!ephemeris->read(ephemeris->context, offset, bytes, sizeof(bytes))) {
    return false;
  }
  for (unsigned i = 0; i < 7; ++i) {
    elements[i] = f64(bytes + 8 * i);
    if (!isfinite(elements[i])) {
      return false;
    }
  }
  return elements[1] > 0 && elements[2] >= 0 && elements[2] < 1;
}

static bool minor_heliocentric(const double elements[7], double jd_tt, Vector *vector) {
  // Meeus chapters 30 and 33; heliocentric osculating elements in J2000.
  double a = elements[1], e = elements[2];
  double mean = wrap(elements[6] * ASTRO_DEG +
                     0.01720209895 * (jd_tt - elements[0]) / (a * sqrt(a)));
  double eccentric = mean;
  bool converged = false;
  for (unsigned i = 0; i < 15; ++i) {
    double correction = (eccentric - e * sin(eccentric) - mean) / (1 - e * cos(eccentric));
    eccentric -= correction;
    if (fabs(correction) < 1e-12) {
      converged = true;
      break;
    }
  }
  if (!converged) {
    return false;
  }
  double true_anomaly = atan2(sqrt(1 - e * e) * sin(eccentric), cos(eccentric) - e);
  double radius = a * (1 - e * cos(eccentric));
  double argument = elements[5] * ASTRO_DEG + true_anomaly;
  double node = elements[4] * ASTRO_DEG, incl = elements[3] * ASTRO_DEG;
  vector->x = radius * (cos(node) * cos(argument) - sin(node) * sin(argument) * cos(incl));
  vector->y = radius * (sin(node) * cos(argument) + cos(node) * sin(argument) * cos(incl));
  vector->z = radius * sin(argument) * sin(incl);
  return true;
}

static double phase_angle(double heliocentric, double geocentric, double earth_sun) {
  return astro_acos(clamp_unit((heliocentric * heliocentric + geocentric * geocentric -
                                earth_sun * earth_sun) / (2 * heliocentric * geocentric)));
}

static double planet_magnitude(unsigned planet, double r, double delta, double phase) {
  double angle = phase / ASTRO_DEG;
  double correction;
  switch (planet) {
    case 0:
      correction = -0.42 + 0.0380 * angle - 0.000273 * angle * angle +
                   0.000002 * angle * angle * angle;
      break;
    case 1:
      correction = -4.40 + 0.0009 * angle + 0.000239 * angle * angle -
                   0.00000065 * angle * angle * angle;
      break;
    case 3: correction = -1.52 + 0.016 * angle; break;
    case 4: correction = -9.40 + 0.005 * angle; break;
    case 5: correction = -8.88 + 0.044 * angle; break;
    case 6: correction = -7.19; break;
    case 7: correction = -6.87; break;
    default: return NAN;
  }
  return correction + 5 * log10(r * delta);
}

static double saturn_ring_correction(Spherical apparent, double jd_tt) {
  // Meeus chapter 45, ring opening angle B, and chapter 41 magnitude correction.
  double t = (jd_tt - 2451545.0) / 36525.0;
  double inclination = (28.075216 - 0.012998 * t + 0.000004 * t * t) * ASTRO_DEG;
  double node = (169.508470 + 1.394681 * t + 0.000412 * t * t) * ASTRO_DEG;
  double sin_b = fabs(sin(inclination) * cos(apparent.latitude) *
                      sin(apparent.longitude - node) -
                      cos(inclination) * sin(apparent.latitude));
  return -2.60 * sin_b + 1.25 * sin_b * sin_b;
}

static double minor_magnitude(unsigned body, double r, double delta, double phase) {
  // JPL SBDB H,G values; IAU H-G phase function, Meeus chapter 41.
  static const double h[] = {3.25, 5.19, 3.34, 4.12};
  static const double g[] = {0.32, 0.32, 0.12, 0.11};
  double tan_half = astro_tan(phase / 2);
  double logarithm = tan_half > 0 ? log(tan_half) : -INFINITY;
  double phi1 = exp(-3.33 * exp(0.63 * logarithm));
  double phi2 = exp(-1.87 * exp(1.22 * logarithm));
  return h[body] + 5 * log10(r * delta) -
         2.5 * log10((1 - g[body]) * phi1 + g[body] * phi2);
}

bool astro_ephemeris_position(const AstroEphemeris *ephemeris, AstroBodyKind kind,
                              unsigned index, double jd_tt, AstroBodyPosition *result) {
  if (!ephemeris || !result || !isfinite(jd_tt) || (unsigned)kind > ASTRO_MINOR ||
      (kind == ASTRO_PLANET && (index >= 8 || index == 2)) ||
      (kind == ASTRO_MINOR && index >= 4) ||
      (kind == ASTRO_PLUTO && (jd_tt < 2409542.5 || jd_tt >= 2488069.5))) {
    return false;
  }
  Spherical earth;
  if (!vsop(ephemeris, 2, jd_tt, &earth)) {
    return false;
  }
  result->outside_minor_epochs = false;
  result->magnitude_known = true;
  if (kind == ASTRO_SUN) {
    Spherical sun = {wrap(earth.longitude + ASTRO_PI), -earth.latitude, earth.radius};
    ecliptic_fk5(&sun, jd_tt);
    sun.longitude -= 20.4898 * ARCSEC / earth.radius;
    result->equatorial = apparent_ecliptic(sun, jd_tt);
    result->magnitude = -26.74 + 5 * log10(earth.radius);
    return true;
  }
  if (kind == ASTRO_MOON) {
    double distance_km;
    Spherical moon;
    if (!astro_moon_ecliptic(ephemeris, jd_tt, &moon.longitude, &moon.latitude,
                             &distance_km)) {
      return false;
    }
    moon.radius = distance_km / AU_KM;
    result->equatorial = apparent_ecliptic(moon, jd_tt);
    Vector moon_vector = spherical_vector(moon);
    Vector earth_vector = spherical_vector(earth);
    Vector moon_to_sun = (Vector){-earth_vector.x - moon_vector.x,
                                 -earth_vector.y - moon_vector.y,
                                 -earth_vector.z - moon_vector.z};
    double r = length(moon_to_sun);
    double phase = phase_angle(r, moon.radius, earth.radius) / ASTRO_DEG;
    double phase_squared = phase * phase;
    result->magnitude = -12.73 + 0.026 * phase + 4e-9 * phase_squared * phase_squared +
                        5 * log10(r * distance_km / 384400.0);
    return true;
  }
  Vector geocentric;
  double heliocentric_radius;
  if (kind == ASTRO_PLANET) {
    Spherical planet;
    Vector earth_vector = spherical_vector(earth);
    double light_time = 0;
    for (unsigned iteration = 0; iteration < 3; ++iteration) {
      if (!vsop(ephemeris, index, jd_tt - light_time, &planet)) {
        return false;
      }
      geocentric = subtract(spherical_vector(planet), earth_vector);
      light_time = length(geocentric) * LIGHT_DAYS_PER_AU;
    }
    heliocentric_radius = planet.radius;
    Spherical apparent = vector_spherical(geocentric);
    ecliptic_aberration(&apparent, jd_tt);
    ecliptic_fk5(&apparent, jd_tt);
    result->equatorial = apparent_ecliptic(apparent, jd_tt);
    result->magnitude = planet_magnitude(index, heliocentric_radius, apparent.radius,
                                         phase_angle(heliocentric_radius, apparent.radius,
                                                     earth.radius));
    if (index == 5) result->magnitude += saturn_ring_correction(apparent, jd_tt);
  } else {
    Vector earth_vector = earth_j2000(earth, jd_tt);
    Vector body = {0, 0, 0};
    double elements[7] = {0};
    if (kind == ASTRO_MINOR &&
        !minor_elements(ephemeris, index, jd_tt, elements, &result->outside_minor_epochs)) {
      return false;
    }
    double light_time = 0;
    for (unsigned iteration = 0; iteration < 3; ++iteration) {
      if (kind == ASTRO_PLUTO) {
        Spherical pluto;
        if (!pluto_heliocentric(ephemeris, jd_tt - light_time, &pluto)) {
          return false;
        }
        body = spherical_vector(pluto);
      } else if (!minor_heliocentric(elements, jd_tt - light_time, &body)) {
        return false;
      }
      geocentric = subtract(body, earth_vector);
      light_time = length(geocentric) * LIGHT_DAYS_PER_AU;
    }
    heliocentric_radius = length(body);
    Spherical astrometric = vector_spherical(geocentric);
    result->equatorial = ecliptic_equatorial(astrometric, 23.4392911111 * ASTRO_DEG);
    astro_precess_j2000(jd_tt, &result->equatorial);
    astro_apply_annual_aberration(jd_tt, &result->equatorial);
    astro_apply_nutation(jd_tt, &result->equatorial);
    if (kind == ASTRO_MINOR) {
      result->magnitude = minor_magnitude(index, heliocentric_radius, astrometric.radius,
                                          phase_angle(heliocentric_radius, astrometric.radius,
                                                      earth.radius));
    } else {
      result->magnitude = -1.0 + 5 * log10(heliocentric_radius * astrometric.radius);
    }
  }
  result->magnitude_known = isfinite(result->magnitude);
  return true;
}
