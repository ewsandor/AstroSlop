#include "../../src/c/astro/ephemeris.h"

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
  AstroBodyKind kind;
  unsigned index;
  double jd, latitude, longitude, elevation, azimuth, altitude;
  bool refraction;
} Reference;

// NASA/JPL Horizons observer ephemerides, retrieved 2026-11-16.
// Geodetic coordinates are east-positive; times are UTC Julian dates.
// Refraction uses Horizons' standard atmosphere; the high-altitude site is airless.
static const Reference REFERENCES[] = {
  // Greenwich, UK: refracted.
  {ASTRO_SUN, 0, 2461042.00, 51.4779, 0, 0, 179.148679, 15.598852, true},
  {ASTRO_SUN, 0, 2464845.75, 51.4779, 0, 0, 76.164354, 17.505346, true},
  {ASTRO_MOON, 0, 2461041.50, 51.4779, 0, 0, 242.031557, 52.042036, true},
  {ASTRO_MOON, 0, 2461042.25, 51.4779, 0, 0, 90.028199, 35.960695, true},
  {ASTRO_MOON, 0, 2462867.50, 51.4779, 0, 0, 274.526108, 8.179413, true},
  {ASTRO_MOON, 0, 2464845.50, 51.4779, 0, 0, 279.123532, 5.942508, true},
  {ASTRO_PLANET, 3, 2461042.00, 51.4779, 0, 0, 177.025173, 14.846509, true},
  {ASTRO_PLANET, 3, 2462867.75, 51.4779, 0, 0, 167.436489, 30.631079, true},
  {ASTRO_PLANET, 5, 2461042.00, 51.4779, 0, 0, 103.038152, 5.817883, true},
  {ASTRO_PLANET, 5, 2461042.25, 51.4779, 0, 0, 196.842008, 33.693243, true},
  {ASTRO_PLANET, 5, 2462867.50, 51.4779, 0, 0, 237.982526, 45.171131, true},
  {ASTRO_MINOR, 0, 2461042.00, 51.4779, 0, 0, 165.146297, 14.714695, true},
  {ASTRO_MINOR, 0, 2462867.50, 51.4779, 0, 0, 241.833069, 36.791114, true},
  {ASTRO_MINOR, 0, 2464845.50, 51.4779, 0, 0, 190.984116, 25.934172, true},
  // Chile: airless, with a 2.2 km observer elevation.
  {ASTRO_SUN, 0, 2461041.50, -30.169, -70.806, 2200, 241.022551, -3.107273, false},
  {ASTRO_SUN, 0, 2461041.75, -30.169, -70.806, 2200, 159.560540, -33.996353, false},
  {ASTRO_SUN, 0, 2461042.00, -30.169, -70.806, 2200, 102.399134, 26.493709, false},
  {ASTRO_SUN, 0, 2461042.25, -30.169, -70.806, 2200, 289.500398, 72.162972, false},
  {ASTRO_SUN, 0, 2462867.50, -30.169, -70.806, 2200, 240.998628, -3.119202, false},
  {ASTRO_SUN, 0, 2462867.75, -30.169, -70.806, 2200, 159.539277, -33.976221, false},
  {ASTRO_SUN, 0, 2464845.50, -30.169, -70.806, 2200, 280.912226, -27.347335, false},
  {ASTRO_SUN, 0, 2464845.75, -30.169, -70.806, 2200, 109.808170, -70.607517, false},
  {ASTRO_MOON, 0, 2461041.50, -30.169, -70.806, 2200, 33.523263, 23.784116, false},
  {ASTRO_MOON, 0, 2461041.75, -30.169, -70.806, 2200, 313.558869, 13.208320, false},
  {ASTRO_MOON, 0, 2461042.00, -30.169, -70.806, 2200, 276.515624, -54.028469, false},
  {ASTRO_MOON, 0, 2461042.25, -30.169, -70.806, 2200, 82.061862, -52.182264, false},
  {ASTRO_MOON, 0, 2462867.50, -30.169, -70.806, 2200, 335.014768, 46.114626, false},
  {ASTRO_MOON, 0, 2462867.75, -30.169, -70.806, 2200, 272.460978, -18.627632, false},
  {ASTRO_MOON, 0, 2464845.50, -30.169, -70.806, 2200, 329.291679, 42.971719, false},
  {ASTRO_MOON, 0, 2464845.75, -30.169, -70.806, 2200, 269.312853, -22.194689, false},
  {ASTRO_PLANET, 3, 2461041.50, -30.169, -70.806, 2200, 241.674499, -0.900772, false},
  {ASTRO_PLANET, 3, 2461041.75, -30.169, -70.806, 2200, 162.264251, -33.978281, false},
  {ASTRO_PLANET, 3, 2461042.00, -30.169, -70.806, 2200, 104.038356, 24.885198, false},
  {ASTRO_PLANET, 3, 2461042.25, -30.169, -70.806, 2200, 290.553281, 74.269817, false},
  {ASTRO_PLANET, 3, 2462867.50, -30.169, -70.806, 2200, 166.758299, -51.882706, false},
  {ASTRO_PLANET, 3, 2462867.75, -30.169, -70.806, 2200, 92.092923, 10.767800, false},
  {ASTRO_PLANET, 3, 2464845.50, -30.169, -70.806, 2200, 300.825944, 5.037807, false},
  {ASTRO_PLANET, 3, 2464845.75, -30.169, -70.806, 2200, 255.268655, -69.963991, false},
  {ASTRO_PLANET, 5, 2461041.50, -30.169, -70.806, 2200, 304.543302, 49.414487, false},
  {ASTRO_PLANET, 5, 2461041.75, -30.169, -70.806, 2200, 248.833418, -25.772349, false},
  {ASTRO_PLANET, 5, 2461042.00, -30.169, -70.806, 2200, 131.190248, -43.845822, false},
  {ASTRO_PLANET, 5, 2461042.25, -30.169, -70.806, 2200, 75.211927, 30.232263, false},
  {ASTRO_PLANET, 5, 2462867.50, -30.169, -70.806, 2200, 36.013430, 32.351033, false},
  {ASTRO_PLANET, 5, 2462867.75, -30.169, -70.806, 2200, 303.054399, 15.338363, false},
  {ASTRO_PLANET, 5, 2464845.50, -30.169, -70.806, 2200, 313.930159, 27.425136, false},
  {ASTRO_PLANET, 5, 2464845.75, -30.169, -70.806, 2200, 264.927706, -44.932543, false},
  {ASTRO_MINOR, 0, 2461041.50, -30.169, -70.806, 2200, 248.633753, 8.407101, false},
  {ASTRO_MINOR, 0, 2461041.75, -30.169, -70.806, 2200, 175.944741, -37.093227, false},
  {ASTRO_MINOR, 0, 2461042.00, -30.169, -70.806, 2200, 108.218449, 14.090894, false},
  {ASTRO_MINOR, 0, 2461042.25, -30.169, -70.806, 2200, 335.432504, 81.730319, false},
  {ASTRO_MINOR, 0, 2462867.50, -30.169, -70.806, 2200, 31.684963, 40.513509, false},
  {ASTRO_MINOR, 0, 2462867.75, -30.169, -70.806, 2200, 294.236103, 12.972270, false},
  {ASTRO_MINOR, 0, 2464845.50, -30.169, -70.806, 2200, 85.948488, 31.215826, false},
  {ASTRO_MINOR, 0, 2464845.75, -30.169, -70.806, 2200, 297.111586, 57.136292, false},
  // Tromso, Norway: refracted, at high northern latitude.
  {ASTRO_SUN, 0, 2464845.75, 69.65, 18.9553, 30, 100.285359, 27.457277, true},
  {ASTRO_MOON, 0, 2461041.50, 69.65, 18.9553, 30, 246.704559, 35.535337, true},
  {ASTRO_MOON, 0, 2461041.75, 69.65, 18.9553, 30, 325.941187, 9.613121, true},
  {ASTRO_MOON, 0, 2461042.00, 69.65, 18.9553, 30, 42.487801, 12.176986, true},
  {ASTRO_MOON, 0, 2461042.25, 69.65, 18.9553, 30, 122.916219, 39.930409, true},
  {ASTRO_PLANET, 3, 2462867.75, 69.65, 18.9553, 30, 188.226183, 13.041443, true},
  {ASTRO_PLANET, 3, 2464845.75, 69.65, 18.9553, 30, 62.125836, 14.868954, true},
  {ASTRO_PLANET, 5, 2461042.00, 69.65, 18.9553, 30, 122.123987, 7.524759, true},
  {ASTRO_PLANET, 5, 2461042.25, 69.65, 18.9553, 30, 213.916569, 13.529996, true},
  {ASTRO_PLANET, 5, 2462867.50, 69.65, 18.9553, 30, 246.060828, 28.433059, true},
  {ASTRO_MINOR, 0, 2462867.50, 69.65, 18.9553, 30, 251.457177, 21.046165, true},
  {ASTRO_MINOR, 0, 2464845.50, 69.65, 18.9553, 30, 208.515380, 6.001459, true},
};

int main(int argc, char **argv) {
  assert(argc == 2);
  FILE *file = fopen(argv[1], "rb");
  assert(file);
  AstroEphemeris ephemeris;
  assert(astro_ephemeris_init(&ephemeris, read_file, file));
  double max_az = 0, max_el = 0;
  for (size_t i = 0; i < sizeof(REFERENCES) / sizeof(REFERENCES[0]); ++i) {
    const Reference *reference = &REFERENCES[i];
    AstroObserver observer = {reference->latitude, reference->longitude, reference->elevation};
    AstroBodyPosition position;
    AstroHorizontal horizontal;
    assert(astro_ephemeris_position(&ephemeris, reference->kind, reference->index,
                                   astro_julian_tt(reference->jd), &position));
    assert(astro_horizontal(reference->jd, &observer, &position.equatorial,
                            reference->refraction, 10, &horizontal));
    assert(horizontal.azimuth_defined);
    double az_error = fabs(remainder(horizontal.azimuth_deg - reference->azimuth, 360));
    double el_error = fabs(horizontal.elevation_deg - reference->altitude);
    double tolerance = reference->kind == ASTRO_MINOR ? 0.1 : 0.01;
    if (az_error > tolerance || el_error > tolerance) {
      fprintf(stderr, "Horizons case %zu: az error %.6f, el error %.6f (limit %.3f)\n",
              i, az_error, el_error, tolerance);
      return 1;
    }
    max_az = fmax(max_az, az_error);
    max_el = fmax(max_el, el_error);
  }
  printf("Horizons pointing tests passed: %zu cases; max az %.5f, el %.5f degrees\n",
         sizeof(REFERENCES) / sizeof(REFERENCES[0]), max_az, max_el);
  fclose(file);
  return 0;
}
