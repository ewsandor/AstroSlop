#pragma once

#include <pebble.h>

typedef struct {
  uint8_t version;
  uint8_t label_mode;
  uint8_t show_time;
  uint8_t show_event;
  uint8_t show_magnitude;
  uint8_t use_weather;
  uint8_t precise;
  uint8_t banner;
  uint8_t inverted;
  uint8_t refresh_seconds;
  uint8_t min_elevation_enabled;
  int8_t min_elevation;
  uint8_t max_magnitude_enabled;
  int8_t max_magnitude;
  uint8_t refraction;
  uint8_t red_tint;
  uint8_t flashlight_enabled;
  uint8_t flashlight_minutes;
} Settings;

void settings_load(Settings *settings);
bool settings_apply(Settings *settings, DictionaryIterator *iterator);
