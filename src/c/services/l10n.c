#include "l10n.h"

#include "../../../resources/generated/catalog_credits.inc"

const char *l10n(StringId id) {
  static const char *const english[STR_COUNT] = {
    [STR_AZ] = "AZ",
    [STR_EL] = "EL",
    [STR_RISE] = "Rise",
    [STR_SET] = "Set",
    [STR_CIRCUMPOLAR] = "Circumpolar",
    [STR_NEVER_RISES] = "Never rises",
    [STR_NO_GPS] = "No GPS fix",
    [STR_LOCATION_DENIED] = "Location denied",
    [STR_CLOCK_UNSET] = "Clock not set",
    [STR_NO_MATCHES] = "No objects match filters",
    [STR_BELOW_SKIPPED] = "Below horizon skipped",
    [STR_FILTER_NEEDS_LOCATION] = "Elevation filter needs location",
    [STR_UPDATING_LOCATION] = "Updating location...",
    [STR_ALTITUDE_UNKNOWN] = "GPS altitude unknown: using sea level",
    [STR_FLASHLIGHT_ON] = "Flashlight on",
    [STR_FLASHLIGHT_OFF] = "Flashlight off",
    [STR_FLASHLIGHT_UNAVAILABLE] = "Flashlight unavailable",
    [STR_POSITION_UNAVAILABLE] = "Position unavailable",
    [STR_EVALUATION_FAILED] = "Unable to evaluate objects",
    [STR_CATALOG_UNAVAILABLE] = "Catalog unavailable",
    [STR_MAGNITUDE] = "Magnitude",
    [STR_MAG_SHORT] = "mag",
    [STR_ABOUT_TITLE] = "About Astronomy Tools",
    [STR_ABOUT_BODY] =
      "Astronomy Tools 0.0.1\nMinor planets:\n2026-2036\n\n"
      "Algorithms: J. Meeus, Astronomical Algorithms; VSOP87, Bretagnon & Francou / IMCCE; "
      "Sonia Keys (MIT).\n\n"
      CATALOG_CREDITS
      "Icons: Pebble iconography (Apache-2.0).\n\n"
      "Catalog data for personal and scientific use only; review source terms.\n"
      "Complete credits: settings page and README.",
  };
  return (unsigned)id < STR_COUNT ? english[id] : "";
}
