#include "events.h"

#include <math.h>

#define TWO_PI (2.0 * ASTRO_PI)
#define SIDEREAL_RADIANS_PER_DAY (TWO_PI * 1.00273790935)
#define AU_KM 149597870.7
#define SEARCH_STEP (3.0 / 24.0)
#define EDGE_STEP (1.0 / 1440.0)

#define wrap astro_wrap

bool astro_event_interpolate(void *context, double jd_utc, AstroEquatorial *position) {
  const AstroEventPositions *samples = context;
  if (!samples || !position || !isfinite(jd_utc) ||
      !isfinite(samples->start_jd_utc)) return false;
  double x = (jd_utc - samples->start_jd_utc) * 3;
  if (x < -1e-8 || x > ASTRO_EVENT_SAMPLES - 1 + 1e-8) return false;
  if (x < 0) x = 0;
  if (x > ASTRO_EVENT_SAMPLES - 1) x = ASTRO_EVENT_SAMPLES - 1;
  unsigned first = x < 1 ? 0 : (unsigned)x - 1;
  if (first > ASTRO_EVENT_SAMPLES - 4) first = ASTRO_EVENT_SAMPLES - 4;
  double t = x - first;
  // Four-point Lagrange interpolation over adjacent eight-hour samples.
  double weights[4] = {
    -(t - 1) * (t - 2) * (t - 3) / 6,
    t * (t - 2) * (t - 3) / 2,
    -t * (t - 1) * (t - 3) / 2,
    t * (t - 1) * (t - 2) / 6,
  };
  double ra = 0, dec = 0, distance = 0;
  double anchor = samples->positions[first].ra;
  for (unsigned i = 0; i < 4; ++i) {
    const AstroEquatorial *p = &samples->positions[first + i];
    if (!isfinite(p->ra) || p->ra < 0 || p->ra >= TWO_PI ||
        !isfinite(p->dec) || !isfinite(p->distance_au) ||
        fabs(p->dec) > ASTRO_PI / 2 || p->distance_au < 0) return false;
    double delta = p->ra - anchor;
    if (delta > ASTRO_PI) delta -= TWO_PI;
    if (delta < -ASTRO_PI) delta += TWO_PI;
    ra += weights[i] * delta;
    dec += weights[i] * p->dec;
    distance += weights[i] * p->distance_au;
  }
  *position = (AstroEquatorial){wrap(anchor + ra), dec, distance};
  return isfinite(position->ra) && isfinite(dec) && isfinite(distance) &&
         fabs(dec) <= ASTRO_PI / 2 && distance >= 0;
}

static double standard_altitude(bool sun, bool moon, double distance_au) {
  if (moon && distance_au > 0) {
    double ratio = (6378.14 / AU_KM) / distance_au;
    if (ratio > 0 && ratio < 1) {
      return 0.7275 * astro_asin(ratio) - 0.5667 * ASTRO_DEG;
    }
  }
  return (sun ? -0.8333 : -0.5667) * ASTRO_DEG;
}

static bool hour_angle(const AstroObserver *observer, const AstroEquatorial *position,
                       bool sun, bool moon, double *h0, AstroEventKind *state) {
  double latitude = observer->latitude_deg * ASTRO_DEG;
  double altitude = standard_altitude(sun, moon, position->distance_au);
  double numerator = sin(altitude) - sin(latitude) * sin(position->dec);
  double denominator = cos(latitude) * cos(position->dec);
  if (fabs(denominator) < 1e-14) {
    *state = numerator > 0 ? ASTRO_EVENT_NEVER_RISES : ASTRO_EVENT_CIRCUMPOLAR;
    return false;
  }
  double cosine = numerator / denominator;
  if (cosine > 1) {
    *state = ASTRO_EVENT_NEVER_RISES;
    return false;
  }
  if (cosine < -1) {
    *state = ASTRO_EVENT_CIRCUMPOLAR;
    return false;
  }
  *h0 = astro_acos(cosine);
  *state = ASTRO_EVENT_NONE;
  return true;
}

static bool refine(double now, const AstroObserver *observer, AstroPositionAt position_at,
                   void *context, bool sun, bool moon, bool rise, double guess,
                   double *result) {
  for (unsigned iteration = 0; iteration < 5; ++iteration) {
    AstroEquatorial position;
    double h0;
    AstroEventKind state;
    if (!position_at(context, guess, &position) ||
        !hour_angle(observer, &position, sun, moon, &h0, &state)) {
      return false;
    }
    double h = astro_local_sidereal(guess, observer->longitude_deg) - position.ra;
    double target = rise ? -h0 : h0;
    double error = wrap(target - h + ASTRO_PI) - ASTRO_PI;
    double adjustment = error / SIDEREAL_RADIANS_PER_DAY;
    guess += adjustment;
    if (fabs(adjustment) * 86400 < 30) {
      if (guess <= now) {
        guess += TWO_PI / SIDEREAL_RADIANS_PER_DAY;
        continue;
      }
      *result = guess;
      return true;
    }
  }
  return false;
}

typedef struct {
  double time;
  double height;
} EventSample;

static bool sample_height(const AstroObserver *observer, AstroPositionAt position_at,
                          void *context, bool sun, bool moon, double jd,
                          EventSample *sample) {
  AstroEquatorial position;
  if (!position_at(context, jd, &position) || !isfinite(position.ra) ||
      !isfinite(position.dec) || !isfinite(position.distance_au) ||
      fabs(position.dec) > ASTRO_PI / 2 || position.distance_au < 0) {
    return false;
  }
  double latitude = observer->latitude_deg * ASTRO_DEG;
  double hour = astro_local_sidereal(jd, observer->longitude_deg) - position.ra;
  sample->time = jd;
  sample->height = sin(latitude) * sin(position.dec) +
    cos(latitude) * cos(position.dec) * cos(hour) -
    sin(standard_altitude(sun, moon, position.distance_au));
  return isfinite(sample->height);
}

static bool crosses(EventSample left, EventSample right) {
  return (left.height < 0 && right.height >= 0) ||
         (left.height > 0 && right.height <= 0);
}

static bool bisect_event(const AstroObserver *observer, AstroPositionAt position_at,
                         void *context, bool sun, bool moon,
                         EventSample left, EventSample right, AstroEvent *event) {
  event->kind = left.height < 0 ? ASTRO_EVENT_RISE : ASTRO_EVENT_SET;
  while ((right.time - left.time) * 86400.0 > 30.0) {
    EventSample middle;
    if (!sample_height(observer, position_at, context, sun, moon,
                       (left.time + right.time) / 2, &middle)) {
      return false;
    }
    if (crosses(left, middle)) {
      right = middle;
    } else {
      left = middle;
    }
  }
  event->jd_utc = (left.time + right.time) / 2;
  return true;
}

static bool extremum(const AstroObserver *observer, AstroPositionAt position_at,
                     void *context, bool sun, bool moon, bool minimum,
                     double start, double end, EventSample *result) {
  const double ratio = 0.3819660112501051;
  EventSample left, right;
  if (!sample_height(observer, position_at, context, sun, moon,
                     start + ratio * (end - start), &left) ||
      !sample_height(observer, position_at, context, sun, moon,
                     end - ratio * (end - start), &right)) {
    return false;
  }
  for (unsigned i = 0; i < 16; ++i) {
    if (minimum ? left.height < right.height : left.height > right.height) {
      end = right.time;
      right = left;
      if (!sample_height(observer, position_at, context, sun, moon,
                         start + ratio * (end - start), &left)) return false;
    } else {
      start = left.time;
      left = right;
      if (!sample_height(observer, position_at, context, sun, moon,
                         end - ratio * (end - start), &right)) return false;
    }
  }
  *result = (minimum ? left.height < right.height : left.height > right.height) ?
            left : right;
  return true;
}

static bool search_moving_event(double now, const AstroObserver *observer,
                                AstroPositionAt position_at, void *context,
                                bool sun, bool moon, AstroEvent *event) {
  EventSample before, current;
  if (!sample_height(observer, position_at, context, sun, moon, now, &before) ||
      !sample_height(observer, position_at, context, sun, moon, now + EDGE_STEP, &current)) {
    return false;
  }
  if (crosses(before, current)) {
    return bisect_event(observer, position_at, context, sun, moon, before, current, event);
  }
  for (unsigned i = 1; i <= 17; ++i) {
    double offset = i <= 15 ? i * SEARCH_STEP : (i == 16 ? 2.0 - EDGE_STEP : 2.0);
    EventSample next;
    if (!sample_height(observer, position_at, context, sun, moon, now + offset, &next)) {
      return false;
    }
    bool minimum = current.height > 0 && current.height < before.height &&
                   current.height < next.height;
    bool maximum = current.height < 0 && current.height > before.height &&
                   current.height > next.height;
    double variation = fabs(before.height - current.height) +
                       fabs(next.height - current.height);
    if ((minimum || maximum) && fabs(current.height) < variation) {
      EventSample peak;
      if (!extremum(observer, position_at, context, sun, moon, minimum,
                    before.time, next.time, &peak)) return false;
      if (crosses(before, peak)) {
        return bisect_event(observer, position_at, context, sun, moon, before, peak, event);
      }
    }
    if (crosses(current, next)) {
      return bisect_event(observer, position_at, context, sun, moon, current, next, event);
    }
    before = current;
    current = next;
  }
  event->kind = ASTRO_EVENT_NONE;
  event->jd_utc = 0;
  return true;
}

bool astro_next_event(double jd_utc, const AstroObserver *observer,
                      AstroPositionAt position_at, void *context,
                      bool moving, bool sun, bool moon, AstroEvent *event) {
  if (!observer || !position_at || !event || !isfinite(jd_utc) ||
      !isfinite(observer->latitude_deg) || !isfinite(observer->longitude_deg) ||
      observer->latitude_deg < -90 || observer->latitude_deg > 90 ||
      observer->longitude_deg < -180 || observer->longitude_deg > 180) {
    return false;
  }
  AstroEquatorial position;
  if (!position_at(context, jd_utc, &position) || !isfinite(position.ra) ||
      !isfinite(position.dec)) {
    return false;
  }
  double h0;
  AstroEventKind state;
  if (!hour_angle(observer, &position, sun, moon, &h0, &state)) {
    if (moving) {
      return search_moving_event(jd_utc, observer, position_at, context, sun, moon, event);
    }
    event->kind = state;
    event->jd_utc = 0;
    return true;
  }
  // Near a polar boundary, motion can create a brief crossing before the
  // fixed-declination guesses; search the full 48-hour interval instead.
  if (moving && fabs(cos(h0)) > 0.8) {
    return search_moving_event(jd_utc, observer, position_at, context, sun, moon, event);
  }
  double h = astro_local_sidereal(jd_utc, observer->longitude_deg) - position.ra;
  double rise_guess = jd_utc + wrap(-h0 - h) / SIDEREAL_RADIANS_PER_DAY;
  double set_guess = jd_utc + wrap(h0 - h) / SIDEREAL_RADIANS_PER_DAY;
  double rise_time, set_time;
  bool have_rise = refine(jd_utc, observer, position_at, context, sun, moon, true,
                          rise_guess, &rise_time);
  bool have_set = refine(jd_utc, observer, position_at, context, sun, moon, false,
                         set_guess, &set_time);
  if ((!have_rise || !have_set) && moving) {
    return search_moving_event(jd_utc, observer, position_at, context, sun, moon, event);
  }
  if (!have_rise && !have_set) {
    return false;
  }
  event->kind = have_rise && (!have_set || rise_time < set_time) ?
                ASTRO_EVENT_RISE : ASTRO_EVENT_SET;
  event->jd_utc = event->kind == ASTRO_EVENT_RISE ? rise_time : set_time;
  if (event->jd_utc - jd_utc > 2.0) {
    event->kind = ASTRO_EVENT_NONE;
    event->jd_utc = 0;
  }
  return true;
}
