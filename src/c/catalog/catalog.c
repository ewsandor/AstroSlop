#include "catalog.h"

#include <string.h>

static ResHandle s_pack;
static size_t s_pack_size;
static uint8_t s_count;

static uint16_t u16(const uint8_t *p) {
  return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool catalog_resource_read(void *context, uint32_t offset, void *buffer, size_t length) {
  ResHandle resource = context;
  if (!resource) {
    return false;
  }
  size_t size = resource_size(resource);
  if (offset > size || length > size - offset) {
    return false;
  }
  return resource_load_byte_range(resource, offset, buffer, length) == length;
}

bool catalog_init(void) {
  uint8_t header[8];
  s_pack = resource_get_handle(RESOURCE_ID_CATALOG_PACK);
  if (!s_pack) {
    return false;
  }
  s_pack_size = resource_size(s_pack);
  if (!catalog_resource_read(s_pack, 0, header, sizeof(header)) ||
      memcmp(header, "ASCP", 4) != 0 || header[4] != 1 || header[5] == 0 ||
      8u + (uint32_t)header[5] * 12u > s_pack_size) {
    return false;
  }
  s_count = header[5];
  return true;
}

uint8_t catalog_count(void) {
  return s_count;
}

static bool read_string(const Catalog *catalog, uint16_t string_offset,
                        char *destination, size_t capacity) {
  if (!capacity || catalog->strings_offset >= catalog->length ||
      string_offset >= catalog->length - catalog->strings_offset) {
    return false;
  }
  size_t copied = 0;
  uint32_t offset = catalog->strings_offset + string_offset;
  while (offset < catalog->length) {
    uint8_t bytes[32];
    size_t available = catalog->length - offset;
    size_t batch = available < sizeof(bytes) ? available : sizeof(bytes);
    if (!catalog_resource_read(s_pack, catalog->offset + offset, bytes, batch)) {
      return false;
    }
    for (size_t i = 0; i < batch; ++i) {
      if (bytes[i] == 0) {
        destination[copied] = '\0';
        return true;
      }
      if (copied + 1 >= capacity) return false;
      destination[copied++] = (char)bytes[i];
    }
    offset += batch;
  }
  return false;
}

bool catalog_open(uint8_t index, Catalog *catalog) {
  uint8_t directory[12], header[20];
  if (!catalog || index >= s_count ||
      !catalog_resource_read(s_pack, 8u + index * 12u, directory, sizeof(directory))) {
    return false;
  }
  memset(catalog, 0, sizeof(*catalog));
  catalog->offset = u32(directory);
  catalog->length = u32(directory + 4);
  if (directory[8] > CATALOG_SOLAR_SYSTEM || catalog->offset > s_pack_size ||
      catalog->length > s_pack_size - catalog->offset || catalog->length < sizeof(header) ||
      !catalog_resource_read(s_pack, catalog->offset, header, sizeof(header))) {
    return false;
  }
  catalog->provider = (CatalogProvider)directory[8];
  catalog->count = u16(header);
  catalog->record_size = u16(header + 2);
  catalog->records_offset = u32(header + 4);
  catalog->strings_offset = u32(header + 8);
  if (!catalog->count || catalog->record_size !=
      (catalog->provider == CATALOG_FIXED ? 20 : 16) ||
      catalog->records_offset < sizeof(header) ||
      catalog->records_offset > catalog->length ||
      catalog->count > (catalog->length - catalog->records_offset) / catalog->record_size ||
      catalog->strings_offset < catalog->records_offset +
                                catalog->count * catalog->record_size ||
      !read_string(catalog, u16(header + 12), catalog->title, sizeof(catalog->title)) ||
      !read_string(catalog, u16(header + 14), catalog->short_title,
                   sizeof(catalog->short_title)) ||
      !read_string(catalog, u16(header + 16), catalog->credit, sizeof(catalog->credit))) {
    return false;
  }
  return true;
}

bool catalog_object(const Catalog *catalog, uint16_t index, CatalogObject *object) {
  uint8_t record[20];
  if (!catalog || !object || index >= catalog->count ||
      !catalog_resource_read(s_pack, catalog->offset + catalog->records_offset +
                             index * catalog->record_size, record, catalog->record_size)) {
    return false;
  }
  memset(object, 0, sizeof(*object));
  uint16_t id_offset, name_offset;
  if (catalog->provider == CATALOG_FIXED) {
    object->ra_deg = (int32_t)u32(record) / 3600000.0;
    object->dec_deg = (int32_t)u32(record + 4) / 3600000.0;
    object->pm_ra_mas = (int16_t)u16(record + 8);
    object->pm_dec_mas = (int16_t)u16(record + 10);
    int16_t magnitude = (int16_t)u16(record + 12);
    object->magnitude_known = magnitude != INT16_MIN;
    object->magnitude = magnitude / 100.0;
    object->type = record[14];
    id_offset = u16(record + 16);
    name_offset = u16(record + 18);
  } else {
    if (record[0] > ASTRO_MINOR) {
      return false;
    }
    object->body_kind = (AstroBodyKind)record[0];
    object->body_index = record[1];
    id_offset = u16(record + 2);
    name_offset = u16(record + 4);
  }
  return read_string(catalog, id_offset, object->id, sizeof(object->id)) &&
         read_string(catalog, name_offset, object->name, sizeof(object->name));
}

bool catalog_apparent(const Catalog *catalog, const CatalogObject *object,
                      const AstroEphemeris *ephemeris, double jd_tt,
                      AstroBodyPosition *position) {
  if (!catalog || !object || !position) {
    return false;
  }
  if (catalog->provider == CATALOG_FIXED) {
    if (!astro_fixed_apparent(jd_tt, object->ra_deg, object->dec_deg,
                              object->pm_ra_mas, object->pm_dec_mas,
                              &position->equatorial)) {
      return false;
    }
    position->magnitude = object->magnitude;
    position->magnitude_known = object->magnitude_known;
    position->outside_minor_epochs = false;
    return true;
  }
  return astro_ephemeris_position(ephemeris, object->body_kind, object->body_index,
                                  jd_tt, position);
}
