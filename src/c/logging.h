#pragma once

#ifdef ASTRO_DEBUG
#define ASTRO_LOG(LEVEL, ...) do { \
  if ((LEVEL) == APP_LOG_LEVEL_ERROR) APP_LOG(LEVEL, "ASTRO_ERROR " __VA_ARGS__); \
  else APP_LOG(LEVEL, __VA_ARGS__); \
} while (0)
#define ASTRO_LOG_TIMER_FIELDS time_t started_seconds; uint16_t started_ms;
#define ASTRO_LOG_TIMER_START() \
  time_t start_seconds; uint16_t start_ms; time_ms(&start_seconds, &start_ms)
#define ASTRO_LOG_TIMER_START_FIELDS(...) time_ms(__VA_ARGS__)
#define ASTRO_LOG_TIMER_END(seconds, milliseconds) \
  time_t seconds; uint16_t milliseconds; time_ms(&seconds, &milliseconds)
#define ASTRO_LOG_TIMER_ELAPSED(end_seconds, start_seconds, end_ms, start_ms) \
  (((end_seconds) - (start_seconds)) * 1000 + (end_ms) - (start_ms))
#else
#define ASTRO_LOG(LEVEL, ...) do { \
  if ((LEVEL) == APP_LOG_LEVEL_ERROR) APP_LOG(LEVEL, "ASTRO_ERROR " __VA_ARGS__); \
} while (0)
#define ASTRO_LOG_TIMER_FIELDS
#define ASTRO_LOG_TIMER_START() ((void)0)
#define ASTRO_LOG_TIMER_START_FIELDS(...) ((void)0)
#define ASTRO_LOG_TIMER_END(seconds, milliseconds) ((void)0)
#define ASTRO_LOG_TIMER_ELAPSED(...) 0
#endif
