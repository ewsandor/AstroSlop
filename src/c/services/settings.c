#include "settings.h"

#include <string.h>

#define SETTINGS_KEY 1
#define SETTINGS_VERSION 1

static Settings defaults(void) {
  Settings settings = {
    .version = SETTINGS_VERSION,
    .label_mode = 2,
    .show_time = true,
    .show_event = true,
    .show_magnitude = true,
    .use_weather = false,
    .precise = false,
    .banner = true,
    .inverted = false,
    .refresh_seconds = 1,
    .min_elevation_enabled = true,
    .min_elevation = 0,
    .max_magnitude_enabled = false,
    .max_magnitude = 8,
    .refraction = true,
    .red_tint = true,
#if defined(PBL_RGB_BACKLIGHT)
    .flashlight_enabled = true,
#else
    .flashlight_enabled = false,
#endif
    .flashlight_minutes = 5,
  };
  return settings;
}

static bool valid(const Settings *settings) {
  return settings->version == SETTINGS_VERSION && settings->label_mode <= 2 &&
    settings->show_time <= 1 && settings->show_event <= 1 &&
    settings->show_magnitude <= 1 && settings->use_weather <= 1 &&
    settings->precise <= 1 && settings->banner <= 1 && settings->inverted <= 1 &&
    (settings->refresh_seconds == 1 || settings->refresh_seconds == 5 ||
     settings->refresh_seconds == 15 || settings->refresh_seconds == 60) &&
    settings->min_elevation_enabled <= 1 && settings->min_elevation >= -10 &&
    settings->min_elevation <= 60 && settings->max_magnitude_enabled <= 1 &&
    settings->max_magnitude >= -2 && settings->max_magnitude <= 15 &&
    settings->refraction <= 1 && settings->red_tint <= 1 &&
    settings->flashlight_enabled <= 1 &&
    (settings->flashlight_minutes == 1 || settings->flashlight_minutes == 2 ||
     settings->flashlight_minutes == 5 || settings->flashlight_minutes == 10);
}

void settings_load(Settings *settings) {
  *settings = defaults();
  if (!persist_exists(SETTINGS_KEY)) {
    return;
  }
  Settings saved;
  int read = persist_read_data(SETTINGS_KEY, &saved, sizeof(saved));
  if (read != (int)sizeof(saved) || !valid(&saved)) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Invalid saved settings; using defaults");
    return;
  }
  *settings = saved;
}

bool settings_apply(Settings *settings, DictionaryIterator *iterator) {
  Settings next = *settings;
  bool present = false;
#define APPLY(KEY, FIELD, MINIMUM, MAXIMUM) do { \
    Tuple *tuple = dict_find(iterator, MESSAGE_KEY_##KEY); \
    if (tuple) { \
      int32_t value = tuple->value->int32; \
      if (value < (MINIMUM) || value > (MAXIMUM)) { \
        APP_LOG(APP_LOG_LEVEL_ERROR, "Invalid setting " #KEY ": %ld", (long)value); \
        return false; \
      } \
      next.FIELD = value; \
      present = true; \
    } \
  } while (0)
  APPLY(CFG_LABEL_MODE, label_mode, 0, 2);
  APPLY(CFG_SHOW_TIME, show_time, 0, 1);
  APPLY(CFG_SHOW_EVENT, show_event, 0, 1);
  APPLY(CFG_SHOW_MAG, show_magnitude, 0, 1);
  APPLY(CFG_USE_WEATHER, use_weather, 0, 1);
  APPLY(CFG_PRECISION, precise, 0, 1);
  APPLY(CFG_BANNER, banner, 0, 1);
  APPLY(CFG_INVERT, inverted, 0, 1);
  APPLY(CFG_REFRESH, refresh_seconds, 1, 60);
  APPLY(CFG_MIN_ELEV_ENABLED, min_elevation_enabled, 0, 1);
  APPLY(CFG_MIN_ELEV, min_elevation, -10, 60);
  APPLY(CFG_MAX_MAG_ENABLED, max_magnitude_enabled, 0, 1);
  APPLY(CFG_MAX_MAG, max_magnitude, -2, 15);
  APPLY(CFG_REFRACTION, refraction, 0, 1);
  APPLY(CFG_TINT, red_tint, 0, 1);
  APPLY(CFG_FLASHLIGHT_ENABLED, flashlight_enabled, 0, 1);
  APPLY(CFG_FLASHLIGHT_MIN, flashlight_minutes, 1, 10);
#undef APPLY
  if (!present) {
    return false;
  }
  if (!valid(&next)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Rejected unsupported settings combination");
    return false;
  }
  if (memcmp(settings, &next, sizeof(next)) == 0) {
    return false;
  }
  *settings = next;
  int written = persist_write_data(SETTINGS_KEY, settings, sizeof(*settings));
  if (written != (int)sizeof(*settings)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Could not persist settings: %d", written);
  }
  return true;
}
