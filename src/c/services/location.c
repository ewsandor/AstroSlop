#include "location.h"

#include "../logging.h"

#include <string.h>

#define LOCATION_KEY 2
#define LOCATION_VERSION 1
#define REQUEST_TIMEOUT_MS 45000

static LocationFix s_fix;
static bool s_has_fix;
static bool s_phone_ready;
static bool s_requesting;
static bool s_sent;
static bool s_forced;
static bool s_fresh_this_session;
static uint8_t s_error;
static unsigned s_retry_index;
static AppTimer *s_next_timer;
static AppTimer *s_timeout_timer;
static LocationChanged s_changed;

static void cancel_timer(AppTimer **timer) {
  if (*timer) {
    app_timer_cancel(*timer);
    *timer = NULL;
  }
}

static bool valid_fix(const LocationFix *fix) {
  return fix->version == LOCATION_VERSION && fix->source <= 1 &&
         fix->altitude_known <= 1 && fix->latitude_e6 >= -90000000 &&
         fix->latitude_e6 <= 90000000 && fix->longitude_e6 >= -180000000 &&
         fix->longitude_e6 <= 180000000 && fix->elevation_m >= -500 &&
         fix->elevation_m <= 12000 && fix->timestamp > 0;
}

static void notify(bool new_fix) {
  if (s_changed) {
    s_changed(new_fix);
  }
}

static void next_request(void *context) {
  (void)context;
  s_next_timer = NULL;
  location_request(false);
}

static void schedule_next(uint32_t delay_ms) {
  cancel_timer(&s_next_timer);
  s_next_timer = app_timer_register(delay_ms, next_request, NULL);
  if (!s_next_timer) {
    ASTRO_LOG(APP_LOG_LEVEL_ERROR, "Could not schedule location refresh");
  }
}

static void request_failed(uint8_t error) {
  static const uint32_t retry_ms[] = {60000, 120000, 300000, 600000, 900000};
  cancel_timer(&s_timeout_timer);
  s_requesting = false;
  s_sent = false;
  s_error = error;
  unsigned index = s_retry_index < ARRAY_LENGTH(retry_ms) ?
                   s_retry_index++ : ARRAY_LENGTH(retry_ms) - 1;
  schedule_next(retry_ms[index]);
  notify(false);
}

static void request_timeout(void *context) {
  (void)context;
  s_timeout_timer = NULL;
  ASTRO_LOG(APP_LOG_LEVEL_WARNING, "Location request timed out");
  request_failed(3);
}

static void send_request(void) {
  if (!s_phone_ready || !s_requesting || s_sent) {
    return;
  }
  DictionaryIterator *iterator = NULL;
  AppMessageResult status = app_message_outbox_begin(&iterator);
  if (status != APP_MSG_OK || !iterator) {
    ASTRO_LOG(APP_LOG_LEVEL_ERROR, "Location outbox unavailable: %d", (int)status);
    request_failed(3);
    return;
  }
  DictionaryResult written = dict_write_uint8(iterator, MESSAGE_KEY_REQ_LOCATION,
                                               s_forced ? 2 : 1);
  if (written != DICT_OK) {
    ASTRO_LOG(APP_LOG_LEVEL_ERROR, "Could not encode location request: %d", (int)written);
    request_failed(3);
    return;
  }
  status = app_message_outbox_send();
  if (status != APP_MSG_OK) {
    ASTRO_LOG(APP_LOG_LEVEL_ERROR, "Could not send location request: %d", (int)status);
    request_failed(3);
    return;
  }
  s_sent = true;
}

void location_init(LocationChanged changed) {
  s_changed = changed;
  if (persist_exists(LOCATION_KEY)) {
    int read = persist_read_data(LOCATION_KEY, &s_fix, sizeof(s_fix));
    if (read == (int)sizeof(s_fix) && valid_fix(&s_fix)) {
      s_has_fix = true;
    } else {
      ASTRO_LOG(APP_LOG_LEVEL_WARNING, "Ignoring invalid saved location");
    }
  }
  location_request(false);
}

void location_deinit(void) {
  cancel_timer(&s_next_timer);
  cancel_timer(&s_timeout_timer);
}

void location_phone_ready(void) {
  s_phone_ready = true;
  if (s_requesting) {
    send_request();
  } else if (s_error) {
    location_request(false);
  }
}

void location_request(bool forced) {
  cancel_timer(&s_next_timer);
  cancel_timer(&s_timeout_timer);
  s_requesting = true;
  s_sent = false;
  s_forced = forced;
  s_error = 0;
  s_timeout_timer = app_timer_register(REQUEST_TIMEOUT_MS, request_timeout, NULL);
  if (!s_timeout_timer) {
    ASTRO_LOG(APP_LOG_LEVEL_ERROR, "Could not schedule location timeout");
    request_failed(3);
    return;
  }
  notify(false);
  send_request();
}

void location_connection_changed(bool connected) {
  if (connected) {
    if (s_error || s_requesting) {
      location_request(s_forced);
    }
  } else if (s_requesting) {
    ASTRO_LOG(APP_LOG_LEVEL_WARNING, "Phone disconnected during location request");
    request_failed(3);
  }
}

void location_outbox_failed(void) {
  if (s_requesting && s_sent) {
    request_failed(3);
  }
}

void location_process_message(DictionaryIterator *iterator) {
  Tuple *error = dict_find(iterator, MESSAGE_KEY_LOC_ERROR);
  if (error) {
    int32_t code = error->value->int32;
    ASTRO_LOG(APP_LOG_LEVEL_WARNING, "Phone location error: %ld", (long)code);
    request_failed(code >= 1 && code <= 3 ? (uint8_t)code : 3);
    return;
  }
  Tuple *lat = dict_find(iterator, MESSAGE_KEY_LOC_LAT_E6);
  Tuple *lon = dict_find(iterator, MESSAGE_KEY_LOC_LON_E6);
  if (!lat && !lon) {
    return;
  }
  Tuple *alt = dict_find(iterator, MESSAGE_KEY_LOC_ALT_M);
  Tuple *alt_valid = dict_find(iterator, MESSAGE_KEY_LOC_ALT_VALID);
  Tuple *accuracy = dict_find(iterator, MESSAGE_KEY_LOC_ACC_M);
  Tuple *source = dict_find(iterator, MESSAGE_KEY_LOC_SOURCE);
  Tuple *timestamp = dict_find(iterator, MESSAGE_KEY_LOC_TIME);
  if (!lat || !lon || !alt || !alt_valid || !accuracy || !source || !timestamp) {
    ASTRO_LOG(APP_LOG_LEVEL_ERROR, "Incomplete location message");
    request_failed(3);
    return;
  }
  if (source->value->int32 < 0 || source->value->int32 > 1 ||
      alt_valid->value->int32 < 0 || alt_valid->value->int32 > 1 ||
      accuracy->value->int32 < 0 || timestamp->value->int32 <= 0) {
    ASTRO_LOG(APP_LOG_LEVEL_ERROR, "Rejected invalid location metadata");
    request_failed(3);
    return;
  }
  LocationFix fix = {
    .version = LOCATION_VERSION,
    .source = source->value->int32,
    .altitude_known = alt_valid->value->int32,
    .latitude_e6 = lat->value->int32,
    .longitude_e6 = lon->value->int32,
    .elevation_m = alt->value->int32,
    .accuracy_m = accuracy->value->uint32,
    .timestamp = timestamp->value->uint32,
  };
  if (!valid_fix(&fix)) {
    ASTRO_LOG(APP_LOG_LEVEL_ERROR, "Rejected invalid phone location");
    request_failed(3);
    return;
  }
  s_fix = fix;
  s_has_fix = true;
  s_fresh_this_session = true;
  s_requesting = false;
  s_sent = false;
  s_error = 0;
  s_retry_index = 0;
  cancel_timer(&s_timeout_timer);
  int written = persist_write_data(LOCATION_KEY, &s_fix, sizeof(s_fix));
  if (written != (int)sizeof(s_fix)) {
    ASTRO_LOG(APP_LOG_LEVEL_ERROR, "Could not persist location: %d", written);
  }
  if (!s_fix.altitude_known) {
    ASTRO_LOG(APP_LOG_LEVEL_WARNING, "GPS altitude unavailable; assuming sea level");
  }
  schedule_next(60u * 60u * 1000u);
  notify(true);
}

bool location_observer(AstroObserver *observer) {
  if (!s_has_fix || !observer) {
    return false;
  }
  observer->latitude_deg = s_fix.latitude_e6 / 1000000.0;
  observer->longitude_deg = s_fix.longitude_e6 / 1000000.0;
  observer->elevation_m = s_fix.elevation_m;
  return true;
}

LocationStatus location_status(time_t now) {
  if (!s_has_fix) {
    return s_requesting ? LOCATION_ACQUIRING : (s_error ? LOCATION_ERROR : LOCATION_MISSING);
  }
  if ((now > (time_t)s_fix.timestamp && now - (time_t)s_fix.timestamp > 75 * 60) ||
      !s_fix.altitude_known) {
    return LOCATION_WARNING;
  }
  if (s_requesting || !s_fresh_this_session) {
    return LOCATION_ACQUIRING;
  }
  return s_error ? LOCATION_WARNING : LOCATION_VALID;
}

bool location_denied(void) {
  return s_error == 2;
}

bool location_altitude_known(void) {
  return s_has_fix && s_fix.altitude_known;
}
