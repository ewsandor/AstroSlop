#include "../../src/c/astro/ephemeris.h"

#include <stdio.h>
#include <stdlib.h>

static bool read_file(void *context, uint32_t offset, void *destination, size_t length) {
  FILE *file = context;
  return fseek(file, (long)offset, SEEK_SET) == 0 &&
         fread(destination, 1, length, file) == length;
}

int main(int argc, char **argv) {
  if (argc != 2) return 1;
  FILE *file = fopen(argv[1], "rb");
  if (!file) {
    perror(argv[1]);
    return 1;
  }
  AstroEphemeris ephemeris;
  if (!astro_ephemeris_init(&ephemeris, read_file, file)) {
    fprintf(stderr, "Invalid ephemeris resource\n");
    fclose(file);
    return 1;
  }
  int kind, refraction;
  unsigned index;
  double jd, ra, dec, pm_ra, pm_dec;
  AstroObserver observer;
  int fields;
  while ((fields = scanf("%d %u %lf %lf %lf %lf %d %lf %lf %lf %lf", &kind, &index,
                         &jd, &observer.latitude_deg, &observer.longitude_deg,
                         &observer.elevation_m, &refraction, &ra, &dec, &pm_ra, &pm_dec)) == 11) {
    AstroBodyPosition position = {0};
    bool valid = kind < 0 ?
      astro_fixed_apparent(astro_julian_tt(jd), ra, dec, pm_ra, pm_dec,
                           &position.equatorial) :
      astro_ephemeris_position(&ephemeris, kind, index, astro_julian_tt(jd), &position);
    AstroHorizontal horizontal;
    if (!valid || !astro_horizontal(jd, &observer, &position.equatorial,
                                    refraction != 0, 10, &horizontal) ||
        !horizontal.azimuth_defined) {
      fprintf(stderr, "Could not calculate host pointing reference\n");
      fclose(file);
      return 1;
    }
    printf("%.9f %.9f\n", horizontal.azimuth_deg, horizontal.elevation_deg);
  }
  if (fields != EOF) {
    fprintf(stderr, "Invalid pointing reference input\n");
    fclose(file);
    return 1;
  }
  fclose(file);
  return 0;
}
