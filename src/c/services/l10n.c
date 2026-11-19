#include "l10n.h"

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
    [STR_OBJECTS_FILTERED] = "Some catalog objects were filtered. Adjust filters in the Pebble app."
                             "\n\nScroll to continue.",
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
  };
  return (unsigned)id < STR_COUNT ? english[id] : "";
}
