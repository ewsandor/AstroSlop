#pragma once

#include "../astro/astro.h"

#include <pebble.h>

typedef struct {
  uint8_t version;
  uint8_t source;
  uint8_t altitude_known;
  int32_t latitude_e6;
  int32_t longitude_e6;
  int32_t elevation_m;
  uint32_t accuracy_m;
  uint32_t timestamp;
} LocationFix;

typedef enum {
  LOCATION_MISSING,
  LOCATION_ACQUIRING,
  LOCATION_VALID,
  LOCATION_WARNING,
  LOCATION_ERROR,
} LocationStatus;

typedef void (*LocationChanged)(bool new_fix);

void location_init(LocationChanged changed);
void location_deinit(void);
void location_phone_ready(void);
void location_request(bool forced);
void location_connection_changed(bool connected);
void location_outbox_failed(void);
void location_process_message(DictionaryIterator *iterator);
bool location_observer(AstroObserver *observer);
LocationStatus location_status(time_t now);
bool location_denied(void);
bool location_altitude_known(void);
