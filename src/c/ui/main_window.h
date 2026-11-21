#pragma once

#include <pebble.h>

typedef enum {
  STATUS_ICON_NONE,
  STATUS_ICON_LOCATION,
  STATUS_ICON_LOCATION_PROBLEM,
  STATUS_ICON_CLOCK,
  STATUS_ICON_WARNING,
} StatusIcon;

typedef struct {
  bool inverted;
  bool precise;
  bool no_coordinates;
  bool flashlight;
  StatusIcon icon;
  char clock[20];
  char temperature[16];
  char label[100];
  char azimuth[24];
  char elevation[24];
  char event[32];
  char magnitude[16];
  char reason[40];
  bool banner;
  bool working;
  bool filter_notice;
  const char *banner_message;
  const char *banner_catalog;
  const char *banner_id;
  const char *banner_name;
  char banner_coordinates[40];
  uint16_t banner_index;
  uint16_t banner_count;
} ScreenState;

bool ui_init(ClickConfigProvider click_provider);
void ui_deinit(void);
void ui_set_click_config_provider(ClickConfigProvider click_provider);
// The state must remain valid until the next update or ui_deinit().
void ui_update(const ScreenState *state);
void ui_capture_next_frame(uint32_t request);
