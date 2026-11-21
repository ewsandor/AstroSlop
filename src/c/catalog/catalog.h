#pragma once

#include "../astro/ephemeris.h"

#include <pebble.h>

typedef enum {
  CATALOG_FIXED = 0,
  CATALOG_SOLAR_SYSTEM = 1,
} CatalogProvider;

typedef struct {
  uint32_t offset;
  uint32_t length;
  uint32_t records_offset;
  uint32_t strings_offset;
  uint16_t count;
  uint16_t record_size;
  CatalogProvider provider;
  char title[40];
  char short_title[24];
} Catalog;

typedef struct {
  char id[32];
  char name[64];
  double ra_deg;
  double dec_deg;
  double pm_ra_mas;
  double pm_dec_mas;
  double magnitude;
  bool magnitude_known;
  AstroBodyKind body_kind;
  uint8_t body_index;
  uint8_t type;
} CatalogObject;

bool catalog_init(void);
uint8_t catalog_count(void);
bool catalog_open(uint8_t index, Catalog *catalog);
bool catalog_object(const Catalog *catalog, uint16_t index, CatalogObject *object);
bool catalog_apparent(const Catalog *catalog, const CatalogObject *object,
                      const AstroEphemeris *ephemeris, double jd_tt,
                      AstroBodyPosition *position);
bool catalog_resource_read(void *context, uint32_t offset, void *buffer, size_t length);
