#pragma once

#include "astro.h"

typedef bool (*AstroPositionAt)(void *context, double jd_utc,
                                AstroEquatorial *position);

typedef enum {
  ASTRO_EVENT_NONE,
  ASTRO_EVENT_RISE,
  ASTRO_EVENT_SET,
  ASTRO_EVENT_CIRCUMPOLAR,
  ASTRO_EVENT_NEVER_RISES,
} AstroEventKind;

typedef struct {
  AstroEventKind kind;
  double jd_utc;
} AstroEvent;

#define ASTRO_EVENT_SAMPLES 7
#define ASTRO_EVENT_SAMPLE_DAYS (1.0 / 3.0)

typedef struct {
  double start_jd_utc;
  AstroEquatorial positions[ASTRO_EVENT_SAMPLES];
} AstroEventPositions;

bool astro_event_interpolate(void *context, double jd_utc, AstroEquatorial *position);
bool astro_next_event(double jd_utc, const AstroObserver *observer,
                      AstroPositionAt position_at, void *context,
                      bool moving, bool sun, bool moon, AstroEvent *event);
