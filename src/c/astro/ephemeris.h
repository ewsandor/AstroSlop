#pragma once

#include "astro.h"

#include <stddef.h>
#include <stdint.h>

typedef bool (*AstroResourceRead)(void *context, uint32_t offset,
                                  void *destination, size_t length);

typedef struct {
  AstroResourceRead read;
  void *context;
  uint32_t directory_offset;
  uint32_t minor_offset;
  uint32_t meeus_offset;
  double first_epoch;
  uint16_t epoch_count;
  uint16_t epoch_step;
  uint16_t moon_longitude_count;
  uint16_t moon_latitude_count;
  uint16_t pluto_count;
} AstroEphemeris;

typedef enum {
  ASTRO_SUN = 0,
  ASTRO_MOON = 1,
  ASTRO_PLANET = 2,
  ASTRO_PLUTO = 3,
  ASTRO_MINOR = 4,
} AstroBodyKind;

typedef struct {
  AstroEquatorial equatorial;
  double magnitude;
  bool magnitude_known;
  bool outside_minor_epochs;
} AstroBodyPosition;

bool astro_ephemeris_init(AstroEphemeris *ephemeris, AstroResourceRead read,
                          void *context);
bool astro_ephemeris_position(const AstroEphemeris *ephemeris, AstroBodyKind kind,
                              unsigned index, double jd_tt, AstroBodyPosition *result);
bool astro_moon_ecliptic(const AstroEphemeris *ephemeris, double jd_tt, double *longitude,
                         double *latitude, double *distance_km);
