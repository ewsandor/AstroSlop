#include <pebble.h>

#include "astro/astro.h"
#include "astro/ephemeris.h"
#include "astro/events.h"
#include "catalog/catalog.h"
#include "services/l10n.h"
#include "services/location.h"
#include "services/settings.h"
#include "ui/main_window.h"

#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define DEG "\xc2\xb0"
#define ACTIVE_CATALOG_KEY 10
#define CATALOG_SELECTION_KEY 100
#define SOLAR_CACHE_SIZE 14
#define BANNER_MS 1500
#define NOTICE_MS 1000

typedef struct {
  bool valid;
  time_t calculated;
  AstroBodyPosition position;
} PositionCache;

typedef struct {
  AstroEventPositions positions;
  AstroObserver observer;
  time_t requested;
  time_t started_seconds;
  uint16_t started_ms;
  uint8_t sample;
  bool moving;
  bool sun;
  bool moon;
} EventJob;

typedef struct {
  bool ignored_elevation;
  bool skipped_below;
} FilterNotices;

typedef struct {
  CatalogObject object;
  AstroBodyPosition position;
  time_t calculated;
  uint16_t index;
  bool position_known;
  FilterNotices notices;
} ObjectSearch;

typedef enum {
  SEARCH_MATCH,
  SEARCH_NO_MATCH,
  SEARCH_FAILED,
  SEARCH_CATALOG_UNAVAILABLE,
} SearchStatus;

static Settings s_settings;
static AstroEphemeris s_ephemeris;
static bool s_ephemeris_ready;
static bool s_catalogs_ready;
static bool s_ui_ready;
static Catalog s_catalog;
static CatalogObject s_object;
static uint8_t s_catalog_index;
static uint16_t s_object_index;
static bool s_object_valid;
static PositionCache s_solar_cache[SOLAR_CACHE_SIZE];
static PositionCache s_current;
static AstroHorizontal s_horizontal;
static bool s_horizontal_valid;
static bool s_had_position_error;
static AstroEvent s_event;
static bool s_event_valid;
static time_t s_event_calculated;
static AppTimer *s_event_timer;
static EventJob s_event_job;
static AppTimer *s_banner_timer;
static AppTimer *s_flashlight_timer;
static AppTimer *s_save_timer;
static bool s_banner_active;
static bool s_selection_after_banner;
static bool s_skipped_notice_shown;
static bool s_flashlight_on;
static char s_banner_message[80];
static bool s_weather_valid;
static int32_t s_temperature_dc;
static uint8_t s_temperature_unit;
static uint32_t s_weather_expires;
static ScreenState s_screen;

static void update_display(void);
static void calculate_position(bool force);
static void schedule_event(void);
static void show_selection_banner(void);

static void cancel_timer(AppTimer **timer) {
  if (*timer) {
    app_timer_cancel(*timer);
    *timer = NULL;
  }
}

static bool clock_valid(time_t now) {
  return now >= 946684800;
}

static bool weather_available(time_t now) {
  return s_settings.use_weather && s_weather_valid && clock_valid(now) &&
         (uint64_t)now < s_weather_expires;
}

static double temperature_c(time_t now) {
  return weather_available(now) ? s_temperature_dc / 10.0 : 10.0;
}

static void format_time(char *buffer, size_t size, time_t value) {
  struct tm *local = localtime(&value);
  if (!local || !strftime(buffer, size, clock_is_24h_style() ? "%H:%M" : "%I:%M %p", local)) {
    snprintf(buffer, size, "--:--");
    return;
  }
  if (!clock_is_24h_style() && buffer[0] == '0') {
    memmove(buffer, buffer + 1, strlen(buffer));
  }
}

static void format_angle(char *buffer, size_t size, double angle, bool azimuth) {
  int scale = s_settings.precise ? 100 : 10;
  long rounded = lround(angle * scale);
  if (azimuth) {
    rounded %= 360 * scale;
    if (rounded < 0) rounded += 360 * scale;
  }
  bool negative = rounded < 0;
  if (negative) rounded = -rounded;
  snprintf(buffer, size, "%s%ld.%0*ld" DEG, negative ? "-" : "",
           rounded / scale, s_settings.precise ? 2 : 1, rounded % scale);
}

static void format_banner_coordinates(char *buffer, size_t size,
                                      const AstroHorizontal *horizontal) {
  long azimuth = lround(horizontal->azimuth_deg) % 360;
  if (azimuth < 0) azimuth += 360;
  long elevation = lround(horizontal->elevation_deg);
  snprintf(buffer, size, "%s %ld" DEG "  %s %ld" DEG,
           l10n(STR_AZ), azimuth, l10n(STR_EL), elevation);
}

static void format_magnitude(char *buffer, size_t size, double magnitude) {
  long rounded = lround(magnitude * 10);
  bool negative = rounded < 0;
  if (negative) rounded = -rounded;
  snprintf(buffer, size, "%s%ld.%ld", negative ? "-" : "", rounded / 10, rounded % 10);
}

static unsigned position_validity(const CatalogObject *object) {
  if (s_catalog.provider == CATALOG_FIXED) return 3600;
  return object->body_kind == ASTRO_MOON ? 10 : 60;
}

static bool apparent_for(const Catalog *catalog, const CatalogObject *object,
                         uint16_t index, time_t now, AstroBodyPosition *result) {
  if (!clock_valid(now)) return false;
  PositionCache *cache = catalog == &s_catalog && catalog->provider == CATALOG_SOLAR_SYSTEM &&
                         index < SOLAR_CACHE_SIZE ? &s_solar_cache[index] : NULL;
  if (cache && cache->valid && now >= cache->calculated &&
      now - cache->calculated < (time_t)position_validity(object)) {
    *result = cache->position;
    return true;
  }
  if (!catalog_apparent(catalog, object, s_ephemeris_ready ? &s_ephemeris : NULL,
                        astro_julian_tt(astro_julian_day(now)), result)) {
    return false;
  }
  if (cache) {
    *cache = (PositionCache){.valid = true, .calculated = now, .position = *result};
  }
  return true;
}

static void calculate_position(bool force) {
  time_t now = time(NULL);
  s_horizontal_valid = false;
  if (!s_object_valid || !clock_valid(now)) return;
  unsigned interval = position_validity(&s_object);
  if (force || !s_current.valid || now < s_current.calculated ||
      now - s_current.calculated >= (time_t)interval) {
    AstroBodyPosition result;
    if (!apparent_for(&s_catalog, &s_object, s_object_index, now, &result)) {
      s_current.valid = false;
      if (!s_had_position_error) {
        APP_LOG(APP_LOG_LEVEL_ERROR, "Could not compute %s", s_object.id);
        s_had_position_error = true;
      }
      return;
    }
    if (result.outside_minor_epochs && !s_had_position_error) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Minor-planet elements are outside their date range");
    }
    s_had_position_error = result.outside_minor_epochs;
    s_current = (PositionCache){.valid = true, .calculated = now, .position = result};
  }
  AstroObserver observer;
  if (location_observer(&observer)) {
    s_horizontal_valid = astro_horizontal(astro_julian_day(now), &observer,
      &s_current.position.equatorial, s_settings.refraction, temperature_c(now), &s_horizontal);
    if (!s_horizontal_valid) {
      APP_LOG(APP_LOG_LEVEL_ERROR, "Could not compute horizontal coordinates");
    }
  }
}

static bool position_at(void *context, double jd_utc, AstroEquatorial *position) {
  (void)context;
  AstroBodyPosition result;
  if (!catalog_apparent(&s_catalog, &s_object, s_ephemeris_ready ? &s_ephemeris : NULL,
                        astro_julian_tt(jd_utc), &result)) {
    return false;
  }
  *position = result.equatorial;
  return true;
}

static void event_callback(void *context) {
  (void)context;
  s_event_timer = NULL;
  if (s_event_job.moving && s_event_job.sample < ASTRO_EVENT_SAMPLES) {
    unsigned index = s_event_job.sample++;
    double jd = s_event_job.positions.start_jd_utc + index * ASTRO_EVENT_SAMPLE_DAYS;
    if (!position_at(NULL, jd, &s_event_job.positions.positions[index])) {
      s_event_calculated = time(NULL);
      APP_LOG(APP_LOG_LEVEL_WARNING, "Could not calculate rise/set position");
      update_display();
      return;
    }
    if (s_event_job.sample < ASTRO_EVENT_SAMPLES) {
      s_event_timer = app_timer_register(1, event_callback, NULL);
      if (!s_event_timer) {
        s_event_calculated = time(NULL);
        APP_LOG(APP_LOG_LEVEL_ERROR, "Could not schedule rise/set calculation");
      }
      return;
    }
  }
  AstroPositionAt event_position = s_event_job.moving ? astro_event_interpolate : position_at;
  void *event_context = s_event_job.moving ? &s_event_job.positions : NULL;
  s_event_valid = astro_next_event(s_event_job.positions.start_jd_utc, &s_event_job.observer,
                                   event_position, event_context, s_event_job.moving,
                                   s_event_job.sun, s_event_job.moon, &s_event);
  time_t end_seconds;
  uint16_t end_ms;
  time_ms(&end_seconds, &end_ms);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Rise/set calculation: %ld ms",
          (long)((end_seconds - s_event_job.started_seconds) * 1000 +
                 end_ms - s_event_job.started_ms));
  s_event_calculated = s_event_job.requested;
  if (!s_event_valid) APP_LOG(APP_LOG_LEVEL_WARNING, "Could not calculate next rise/set");
  update_display();
}

static void schedule_event(void) {
  cancel_timer(&s_event_timer);
  s_event_valid = false;
  time_t now = time(NULL);
  if (!s_settings.show_event || !s_object_valid || !clock_valid(now)) return;
  AstroObserver observer;
  if (!location_observer(&observer)) return;
  s_event_job = (EventJob){
    .positions.start_jd_utc = astro_julian_day(now),
    .observer = observer,
    .requested = now,
    .moving = s_catalog.provider == CATALOG_SOLAR_SYSTEM,
    .sun = s_catalog.provider == CATALOG_SOLAR_SYSTEM && s_object.body_kind == ASTRO_SUN,
    .moon = s_catalog.provider == CATALOG_SOLAR_SYSTEM && s_object.body_kind == ASTRO_MOON,
  };
  time_ms(&s_event_job.started_seconds, &s_event_job.started_ms);
  s_event_timer = app_timer_register(20, event_callback, NULL);
  if (!s_event_timer) {
    s_event_calculated = now;
    APP_LOG(APP_LOG_LEVEL_ERROR, "Could not schedule rise/set calculation");
  }
}

static void update_display(void) {
  if (!s_ui_ready) return;
  time_t now = time(NULL);
  memset(&s_screen, 0, sizeof(s_screen));
  s_screen.inverted = s_settings.inverted;
  s_screen.precise = s_settings.precise;
  s_screen.flashlight = s_flashlight_on;
  if (s_settings.show_time && clock_valid(now)) {
    format_time(s_screen.clock, sizeof(s_screen.clock), now);
  }
  if (weather_available(now)) {
    long degrees = s_temperature_unit ?
      lround(s_temperature_dc * 9.0 / 50.0 + 32.0) : lround(s_temperature_dc / 10.0);
    snprintf(s_screen.temperature, sizeof(s_screen.temperature), "%ld" DEG "%c", degrees,
             s_temperature_unit ? 'F' : 'C');
  }
  if (s_object_valid) {
    if (s_settings.label_mode == 0) {
      snprintf(s_screen.label, sizeof(s_screen.label), "%s",
               s_object.id[0] ? s_object.id : s_object.name);
    } else if (s_settings.label_mode == 1) {
      snprintf(s_screen.label, sizeof(s_screen.label), "%s",
               s_object.name[0] ? s_object.name : s_object.id);
    } else {
      snprintf(s_screen.label, sizeof(s_screen.label), "%s%s%s", s_object.id,
               s_object.id[0] && s_object.name[0] ? "  " : "", s_object.name);
    }
  }
  AstroObserver observer;
  bool has_location = location_observer(&observer);
  LocationStatus location = location_status(now);
  s_screen.no_coordinates = !clock_valid(now) || !has_location ||
                            !s_horizontal_valid || !s_horizontal.azimuth_defined;
  if (!has_location && (location == LOCATION_ACQUIRING || location == LOCATION_MISSING)) {
    if (location != LOCATION_ACQUIRING || s_settings.refresh_seconds != 1 || now % 2 == 0) {
      s_screen.icon = STATUS_ICON_LOCATION;
    }
    snprintf(s_screen.reason, sizeof(s_screen.reason), "%s", l10n(STR_NO_GPS));
  } else if (!has_location && location == LOCATION_ERROR) {
    s_screen.icon = STATUS_ICON_LOCATION_PROBLEM;
    snprintf(s_screen.reason, sizeof(s_screen.reason), "%s",
             location_denied() ? l10n(STR_LOCATION_DENIED) : l10n(STR_NO_GPS));
  } else if (!clock_valid(now)) {
    s_screen.icon = STATUS_ICON_CLOCK;
    snprintf(s_screen.reason, sizeof(s_screen.reason), "%s", l10n(STR_CLOCK_UNSET));
  } else if (!s_catalogs_ready || !s_object_valid || (has_location && !s_current.valid)) {
    s_screen.icon = STATUS_ICON_WARNING;
    snprintf(s_screen.reason, sizeof(s_screen.reason), "%s", l10n(STR_POSITION_UNAVAILABLE));
  } else if (location == LOCATION_WARNING) {
    s_screen.icon = STATUS_ICON_WARNING;
  } else if (location == LOCATION_ACQUIRING || location == LOCATION_MISSING) {
    s_screen.icon = STATUS_ICON_LOCATION;
    snprintf(s_screen.reason, sizeof(s_screen.reason), "%s", l10n(STR_NO_GPS));
  }
  if (s_horizontal_valid) {
    if (s_horizontal.azimuth_defined) {
      format_angle(s_screen.azimuth, sizeof(s_screen.azimuth), s_horizontal.azimuth_deg, true);
    } else {
      snprintf(s_screen.azimuth, sizeof(s_screen.azimuth), "---.%s" DEG,
               s_settings.precise ? "--" : "-");
      s_screen.icon = STATUS_ICON_WARNING;
    }
    format_angle(s_screen.elevation, sizeof(s_screen.elevation), s_horizontal.elevation_deg, false);
  } else {
    snprintf(s_screen.azimuth, sizeof(s_screen.azimuth), "---.%s" DEG,
             s_settings.precise ? "--" : "-");
    snprintf(s_screen.elevation, sizeof(s_screen.elevation), "---.%s" DEG,
             s_settings.precise ? "--" : "-");
  }
  if (!s_screen.no_coordinates && s_settings.show_event && s_event_valid) {
    if (s_event.kind == ASTRO_EVENT_RISE || s_event.kind == ASTRO_EVENT_SET) {
      time_t event_time = (time_t)floor((s_event.jd_utc - 2440587.5) * 86400.0 + 30.0);
      char formatted[20];
      format_time(formatted, sizeof(formatted), event_time);
      snprintf(s_screen.event, sizeof(s_screen.event), "%s %s",
               l10n(s_event.kind == ASTRO_EVENT_RISE ? STR_RISE : STR_SET), formatted);
    } else if (s_event.kind == ASTRO_EVENT_CIRCUMPOLAR) {
      snprintf(s_screen.event, sizeof(s_screen.event), "%s", l10n(STR_CIRCUMPOLAR));
    } else if (s_event.kind == ASTRO_EVENT_NEVER_RISES) {
      snprintf(s_screen.event, sizeof(s_screen.event), "%s", l10n(STR_NEVER_RISES));
    }
  }
  if (s_settings.show_magnitude && s_current.valid && s_current.position.magnitude_known) {
    format_magnitude(s_screen.magnitude, sizeof(s_screen.magnitude), s_current.position.magnitude);
  }
  s_screen.banner = s_banner_active;
  if (s_banner_active) {
    s_screen.banner_message = s_banner_message;
    s_screen.banner_catalog = s_catalog.title;
    s_screen.banner_id = s_object.id;
    s_screen.banner_name = s_object.name;
    s_screen.banner_index = s_object_index + 1;
    s_screen.banner_count = s_catalog.count;
    if (!s_screen.no_coordinates) {
      format_banner_coordinates(s_screen.banner_coordinates,
                                sizeof(s_screen.banner_coordinates), &s_horizontal);
    }
  }
  ui_update(&s_screen);
}

static void banner_finished(void *context) {
  (void)context;
  s_banner_timer = NULL;
  s_banner_active = false;
  if (s_selection_after_banner) {
    s_selection_after_banner = false;
    show_selection_banner();
  } else {
    update_display();
  }
}

static void start_banner(const char *message, bool selection_after) {
  cancel_timer(&s_banner_timer);
  snprintf(s_banner_message, sizeof(s_banner_message), "%s", message ? message : "");
  s_banner_active = true;
  s_selection_after_banner = selection_after && s_settings.banner;
  s_banner_timer = app_timer_register(message ? NOTICE_MS : BANNER_MS, banner_finished, NULL);
  if (!s_banner_timer) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Could not schedule banner dismissal");
    s_banner_active = false;
    s_selection_after_banner = false;
  }
  update_display();
}

static void show_selection_banner(void) {
  if (s_settings.banner && s_object_valid) start_banner(NULL, false);
  else update_display();
}

static void save_selection(void *context) {
  (void)context;
  s_save_timer = NULL;
  int first = persist_write_int(ACTIVE_CATALOG_KEY, s_catalog_index);
  int second = persist_write_int(CATALOG_SELECTION_KEY + s_catalog_index, s_object_index);
  if (first < 0 || second < 0) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Could not save catalog selection");
  }
}

static void save_selection_later(void) {
  cancel_timer(&s_save_timer);
  s_save_timer = app_timer_register(1000, save_selection, NULL);
  if (!s_save_timer) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Could not schedule selection save");
    save_selection(NULL);
  }
}

static SearchStatus find_object(const Catalog *catalog, uint16_t start, int direction,
                                bool include_start, ObjectSearch *result) {
  time_t start_seconds;
  uint16_t start_ms;
  time_ms(&start_seconds, &start_ms);
  time_t now = time(NULL);
  AstroObserver observer;
  bool has_location = location_observer(&observer) && clock_valid(now);
  result->notices.ignored_elevation = s_settings.min_elevation_enabled && !has_location;
  result->notices.skipped_below = false;
  bool failed = false;
  uint16_t searched = 0;
  SearchStatus status = SEARCH_NO_MATCH;
  for (uint32_t step = 0; step < catalog->count; ++step) {
    searched = step + 1;
    int32_t candidate_index = (int32_t)start + direction * (int32_t)(step + !include_start);
    candidate_index = (candidate_index % catalog->count + catalog->count) % catalog->count;
    if (!catalog_object(catalog, candidate_index, &result->object)) {
      APP_LOG(APP_LOG_LEVEL_ERROR, "Could not read catalog entry %ld", (long)candidate_index);
      failed = true;
      break;
    }
    result->position = (AstroBodyPosition){
      .magnitude = result->object.magnitude,
      .magnitude_known = result->object.magnitude_known,
    };
    result->position_known = clock_valid(now) &&
      apparent_for(catalog, &result->object, candidate_index, now, &result->position);
    if (clock_valid(now) && !result->position_known) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Could not evaluate catalog entry %s", result->object.id);
      failed = true;
      continue;
    }
    if (s_settings.max_magnitude_enabled && result->position.magnitude_known &&
        result->position.magnitude > s_settings.max_magnitude) continue;
    if (s_settings.min_elevation_enabled && has_location) {
      AstroHorizontal horizontal;
      if (!astro_horizontal(astro_julian_day(now), &observer, &result->position.equatorial,
                            s_settings.refraction, temperature_c(now), &horizontal)) {
        APP_LOG(APP_LOG_LEVEL_WARNING, "Could not evaluate elevation for %s", result->object.id);
        failed = true;
        continue;
      }
      if (horizontal.elevation_deg < s_settings.min_elevation) {
        if (s_settings.min_elevation == 0) result->notices.skipped_below = true;
        continue;
      }
    }
    result->index = candidate_index;
    result->calculated = now;
    status = SEARCH_MATCH;
    break;
  }
  if (status != SEARCH_MATCH && failed) status = SEARCH_FAILED;
  time_t end_seconds;
  uint16_t end_ms;
  time_ms(&end_seconds, &end_ms);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Catalog search: %u objects, %ld ms",
          searched, (long)((end_seconds - start_seconds) * 1000 + end_ms - start_ms));
  return status;
}

static void show_filter_banner(FilterNotices notices) {
  if (notices.ignored_elevation) {
    start_banner(l10n(STR_FILTER_NEEDS_LOCATION), true);
  } else if (notices.skipped_below && !s_skipped_notice_shown) {
    s_skipped_notice_shown = true;
    start_banner(l10n(STR_BELOW_SKIPPED), true);
  } else {
    show_selection_banner();
  }
}

static void activate_object(const ObjectSearch *selection) {
  s_object_index = selection->index;
  s_object = selection->object;
  s_object_valid = true;
  s_current = (PositionCache){.valid = selection->position_known,
    .calculated = selection->calculated, .position = selection->position};
  s_had_position_error = selection->position_known && selection->position.outside_minor_epochs;
  calculate_position(false);
  schedule_event();
}

static SearchStatus select_catalog(uint8_t catalog_index, bool filter_new,
                                   FilterNotices *notices) {
  Catalog next;
  if (!catalog_open(catalog_index, &next)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Could not open catalog %u", catalog_index);
    return SEARCH_CATALOG_UNAVAILABLE;
  }
  bool saved = persist_exists(CATALOG_SELECTION_KEY + catalog_index);
  int32_t index = saved ? persist_read_int(CATALOG_SELECTION_KEY + catalog_index) : 0;
  if (index < 0 || index >= next.count) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Ignoring invalid saved catalog entry %ld", (long)index);
    index = 0;
    saved = false;
  }
  ObjectSearch selection = {0};
  if (filter_new && !saved) {
    SearchStatus status = find_object(&next, 0, 1, true, &selection);
    if (status != SEARCH_MATCH) return status;
  } else {
    selection.index = index;
    if (!catalog_object(&next, index, &selection.object)) {
      APP_LOG(APP_LOG_LEVEL_ERROR, "Could not open catalog entry %ld", (long)index);
      return SEARCH_CATALOG_UNAVAILABLE;
    }
  }
  s_catalog = next;
  s_catalog_index = catalog_index;
  memset(s_solar_cache, 0, sizeof(s_solar_cache));
  activate_object(&selection);
  if (notices) *notices = selection.notices;
  return SEARCH_MATCH;
}

static void cycle_object(int direction) {
  if (!s_object_valid) return;
  ObjectSearch selection = {0};
  SearchStatus status = find_object(&s_catalog, s_object_index, direction, false, &selection);
  if (status != SEARCH_MATCH) {
    start_banner(l10n(status == SEARCH_FAILED ? STR_EVALUATION_FAILED : STR_NO_MATCHES), false);
    return;
  }
  activate_object(&selection);
  save_selection_later();
  show_filter_banner(selection.notices);
}

static void flashlight_off(void *context) {
  (void)context;
  s_flashlight_timer = NULL;
  if (s_flashlight_on) {
    light_enable(false);
    s_flashlight_on = false;
    update_display();
  }
}

static void apply_backlight(void) {
  light_enable_interaction(!s_settings.disable_backlight);
#if defined(PBL_RGB_BACKLIGHT)
  if (s_settings.red_tint) light_set_color(GColorRed);
  else light_set_system_color();
#endif
  if (!s_settings.flashlight_enabled && s_flashlight_on) {
    cancel_timer(&s_flashlight_timer);
    flashlight_off(NULL);
  }
}

static void toggle_flashlight(void) {
  if (!s_settings.flashlight_enabled) return;
  vibes_short_pulse();
  if (s_flashlight_on) {
    cancel_timer(&s_flashlight_timer);
    flashlight_off(NULL);
    start_banner(l10n(STR_FLASHLIGHT_OFF), false);
  } else {
    s_flashlight_timer = app_timer_register((uint32_t)s_settings.flashlight_minutes * 60000,
                                             flashlight_off, NULL);
    if (!s_flashlight_timer) {
      APP_LOG(APP_LOG_LEVEL_ERROR, "Could not schedule flashlight timeout");
      start_banner(l10n(STR_FLASHLIGHT_UNAVAILABLE), false);
      return;
    }
    light_enable(true);
    s_flashlight_on = true;
    start_banner(l10n(STR_FLASHLIGHT_ON), false);
  }
}

static void up_click(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer; (void)context;
  cycle_object(-1);
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer; (void)context;
  cycle_object(1);
}

static void select_click(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer; (void)context;
  if (!s_catalogs_ready) return;
  cancel_timer(&s_save_timer);
  save_selection(NULL);
  FilterNotices notices = {0};
  SearchStatus status = select_catalog((s_catalog_index + 1) % catalog_count(), true, &notices);
  if (status == SEARCH_MATCH) {
    save_selection_later();
    show_filter_banner(notices);
  } else {
    start_banner(l10n(status == SEARCH_NO_MATCH ? STR_NO_MATCHES :
                      status == SEARCH_FAILED ? STR_EVALUATION_FAILED :
                      STR_CATALOG_UNAVAILABLE), false);
  }
}

static void up_long(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer; (void)context;
  ui_show_about();
}

static void down_long(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer; (void)context;
  vibes_short_pulse();
  location_request(true);
  start_banner(l10n(STR_UPDATING_LOCATION), false);
}

static void select_long(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer; (void)context;
  toggle_flashlight();
}

static void click_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_UP, 700, up_long, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, 700, down_long, NULL);
  window_long_click_subscribe(BUTTON_ID_SELECT, 700, select_long, NULL);
}

static void location_changed(bool new_fix) {
  if (new_fix) {
    calculate_position(false);
    schedule_event();
  }
  update_display();
}

static void connection_changed(bool connected) {
  location_connection_changed(connected);
  update_display();
}

static void tick(struct tm *tick_time, TimeUnits units) {
  (void)tick_time; (void)units;
  time_t now = time(NULL);
  bool weather_expired = false;
  if (s_weather_valid && clock_valid(now) && (uint64_t)now >= s_weather_expires) {
    s_weather_valid = false;
    weather_expired = true;
    APP_LOG(APP_LOG_LEVEL_INFO, "Weather reading expired");
  }
  bool refresh = now % s_settings.refresh_seconds == 0;
  if (refresh || weather_expired) calculate_position(false);
  bool event_passed = s_event_valid &&
    (s_event.kind == ASTRO_EVENT_RISE || s_event.kind == ASTRO_EVENT_SET) &&
    astro_julian_day(now) >= s_event.jd_utc;
  if (s_settings.show_event && !s_event_timer && clock_valid(now) &&
      (event_passed || !s_event_calculated || now < s_event_calculated ||
       now - s_event_calculated >= 600)) {
    schedule_event();
  }
  if (refresh || weather_expired) update_display();
}

static void configure_tick(void) {
  tick_timer_service_unsubscribe();
  tick_timer_service_subscribe(s_settings.refresh_seconds == 60 ? MINUTE_UNIT : SECOND_UNIT, tick);
}

static void process_weather(DictionaryIterator *iterator) {
  Tuple *clear = dict_find(iterator, MESSAGE_KEY_WX_CLEAR);
  if (clear && clear->value->int32) s_weather_valid = false;
  Tuple *temperature = dict_find(iterator, MESSAGE_KEY_WX_TEMP_DC);
  Tuple *unit = dict_find(iterator, MESSAGE_KEY_WX_TEMP_UNIT);
  Tuple *expires = dict_find(iterator, MESSAGE_KEY_WX_EXPIRES);
  if (!temperature && !unit && !expires) return;
  if (!temperature || !unit || !expires ||
      temperature->value->int32 < -1000 || temperature->value->int32 > 800 ||
      unit->value->int32 < 0 || unit->value->int32 > 1 || expires->value->int32 <= 0) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Rejected invalid weather message");
    s_weather_valid = false;
    return;
  }
  s_temperature_dc = temperature->value->int32;
  s_temperature_unit = unit->value->int32;
  s_weather_expires = expires->value->uint32;
  s_weather_valid = true;
}

static void inbox_received(DictionaryIterator *iterator, void *context) {
  (void)context;
  Tuple *ready = dict_find(iterator, MESSAGE_KEY_JS_READY);
  if (ready && ready->value->int32) location_phone_ready();
  location_process_message(iterator);
  bool settings_changed = settings_apply(&s_settings, iterator);
  process_weather(iterator);
  if (settings_changed) {
    configure_tick();
    apply_backlight();
    schedule_event();
  }
  calculate_position(false);
  update_display();
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  (void)context;
  APP_LOG(APP_LOG_LEVEL_ERROR, "Incoming message dropped: %d", (int)reason);
}

static void outbox_failed(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  (void)iterator; (void)context;
  APP_LOG(APP_LOG_LEVEL_WARNING, "Outgoing message failed: %d", (int)reason);
  location_outbox_failed();
}

static void init(void) {
  if (!setlocale(LC_ALL, "")) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Could not initialize system locale");
  }
  settings_load(&s_settings);
  apply_backlight();
  s_catalogs_ready = catalog_init();
  ResHandle ephemeris_resource = resource_get_handle(RESOURCE_ID_EPHEMERIS_PACK);
  s_ephemeris_ready = astro_ephemeris_init(&s_ephemeris, catalog_resource_read,
                                            ephemeris_resource);
  if (!s_catalogs_ready) APP_LOG(APP_LOG_LEVEL_ERROR, "Could not initialize catalog resources");
  if (!s_ephemeris_ready) APP_LOG(APP_LOG_LEVEL_ERROR, "Could not initialize ephemeris resources");
  int32_t saved_catalog = persist_exists(ACTIVE_CATALOG_KEY) ?
                          persist_read_int(ACTIVE_CATALOG_KEY) : 0;
  if (saved_catalog < 0 || saved_catalog >= catalog_count()) saved_catalog = 0;
  if (s_catalogs_ready && select_catalog(saved_catalog, false, NULL) != SEARCH_MATCH) {
    s_catalogs_ready = false;
  }
  if (!ui_init(click_provider)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Could not initialize user interface");
    return;
  }
  s_ui_ready = true;
  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  AppMessageResult result = app_message_open(512, 64);
  if (result != APP_MSG_OK) APP_LOG(APP_LOG_LEVEL_ERROR, "Could not open AppMessage: %d", (int)result);
  location_init(location_changed);
  connection_service_subscribe((ConnectionHandlers){.pebble_app_connection_handler = connection_changed});
  configure_tick();
  calculate_position(true);
  schedule_event();
  update_display();
  APP_LOG(APP_LOG_LEVEL_INFO, "AstroSlop started; free heap: %lu bytes",
          (unsigned long)heap_bytes_free());
}

static void deinit(void) {
  if (s_save_timer) {
    cancel_timer(&s_save_timer);
    save_selection(NULL);
  }
  cancel_timer(&s_event_timer);
  cancel_timer(&s_banner_timer);
  cancel_timer(&s_flashlight_timer);
  flashlight_off(NULL);
  location_deinit();
  tick_timer_service_unsubscribe();
  connection_service_unsubscribe();
  app_message_deregister_callbacks();
  light_enable_interaction(true);
#if defined(PBL_RGB_BACKLIGHT)
  light_set_system_color();
#endif
  ui_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
