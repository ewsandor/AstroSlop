# Telescope PushTo Guide

Telescope PushTo Guide is a Pebble watchapp for visual astronomy. It shows the
live altitude and azimuth of your target so you can aim a Dobsonian or other
manual alt-az telescope with a setting circle and an angle gauge. No motors or
encoders are required. On the watch, look for **PushTo Guide** beside the
telescope icon.

**Status:** Initial implementation; hardware validation remains outstanding.
See [limitations](#limitations-and-open-issues) before using it.

## Features

- Live azimuth and elevation, displayed in degrees to 0.1° by default.
  Azimuth is measured clockwise from true north. Display refresh and optional
  0.01° precision are configurable.
- On-watch catalogs for the Sun, Moon, planets and selected minor planets; the
  100 brightest stars; 110 Messier objects; and the centers of the 88 IAU
  constellations. M102 is represented by NGC 5866.
- Phone GPS at startup and hourly, with manual coordinates as an alternative
  and a persisted last fix. Astronomy calculations run on the watch without
  phone round-trips.
- Configurable object name, local time, next rise or set time, magnitude and
  optional local weather temperature, subject to screen space.
- High-contrast display with an invert option, a configurable red backlight on
  supported watches, and elevation and magnitude filters.
- A timed flashlight, warning icons, a selection banner and on-watch credits.

The app targets Pebble Time 2 (**emery**), Pebble 2 Duo (**flint**) and
Pebble Round 2 (**gabbro**), as well as Pebble Time (**basalt**), Time Round
(**chalk**) and Pebble 2 (**diorite**). Aplite is not supported. Smaller and
round displays may omit secondary fields.

## Controls

| Action | Control |
|---|---|
| Previous / next object | Up / Down |
| Next catalog | Select |
| Refresh GPS and weather subscription | Hold Down |
| Toggle timed flashlight | Hold Select |
| About and credits | Hold Up |

Open the app's settings from its gear icon in the Pebble mobile app.
The settings page uses [Clay](https://github.com/pebble-dev/clay), the SDK's
recommended configuration framework; changes apply when you tap **Save**. The
elevation filter is enabled at 0° by default; disable it to browse objects below the horizon. On watches with
a white backlight, the flashlight is disabled by default. The first visit to a
catalog selects its first matching object; later visits restore the last
selection even if it no longer passes the filters.

## Interface mockups

These design renderings illustrate the main screen and temporary object banner.
They use the same scale; live values and text vary with the selected object.

| | Pebble Time 2 (emery) | Pebble Round 2 (gabbro) |
|---|---|---|
| Main screen | <img src="docs/images/readme/emery_main.png" alt="Emery main screen showing M31 azimuth and elevation" width="200"> | <img src="docs/images/readme/gabbro_main.png" alt="Gabbro main screen showing M31 azimuth and elevation" width="260"> |
| Object banner | <img src="docs/images/readme/emery_banner.png" alt="Emery object banner for M31 Andromeda Galaxy" width="200"> | <img src="docs/images/readme/gabbro_banner.png" alt="Gabbro object banner for M31 Andromeda Galaxy" width="260"> |

More platform and state mockups are in the [design document](docs/DESIGN.md#911-emulator-renderings).

Native-resolution app store screenshots for each supported platform are in
[assets/screenshots](assets/screenshots/). They show sample emulator positions;
the Emery set also includes its red backlight.

## Theory of operation

The watchapp uses the watch's system clock and a location from the Pebble
mobile app (or manual coordinates). For the selected object, it calculates an
apparent celestial position and converts it to local azimuth and elevation
using sidereal time and the observer's latitude and longitude. Solar-system
positions use planetary, lunar or orbital models; fixed objects use catalog
coordinates corrected for proper motion, precession, nutation and aberration.
Topocentric parallax is included. Elevation includes atmospheric refraction by
default, using observer elevation and optionally a weather temperature. The
display also computes the next rise or set event when enabled.

The algorithms are based in part on Jean Meeus, *Astronomical Algorithms*,
2nd edition. See the [astronomy design](docs/DESIGN.md#7-astronomy-algorithms)
for models, assumptions and accuracy targets.

**Solar safety:** Never look at the Sun through a telescope or finder without a
properly installed, purpose-made solar filter.

## Data sources

- [NASA/JPL Horizons](https://ssd.jpl.nasa.gov/horizons/) and the
  [Small-Body Database](https://ssd.jpl.nasa.gov/tools/sbdb_lookup.html):
  minor-planet orbital elements and magnitude data.
- IMCCE/Paris Observatory VSOP87 planetary theory, distributed through CDS
  VizieR; lunar and Pluto models as described by Meeus.
- Yale [Bright Star Catalogue](https://heasarc.gsfc.nasa.gov/W3Browse/star-catalog/bsc5p.html)
  via NASA HEASARC, with names from the
  [IAU Catalog of Star Names](https://www.iau.org/public/themes/naming_stars/).
- NASA HEASARC [Messier catalog](https://heasarc.gsfc.nasa.gov/W3Browse/all/messier.html)
  and NGC 2000.0; IAU constellation boundaries digitized in CDS VizieR VI/49.
- The user's weather provider, via the Pebble mobile app's weather plugin,
  when local weather is enabled.

Source acknowledgements are in [CREDITS.md](CREDITS.md) and the
[design document](docs/DESIGN.md#13-dependencies-data-sources-and-attribution).

## Building and testing

Install the [Pebble SDK](https://developer.repebble.com/sdk/) (4.44), Python 3,
Node.js with npm, and a host C compiler and `make`. The checked-in catalog and
ephemeris inputs are packed automatically. `pebble build` installs the Clay
package from npm on first build; it does not download catalog data. From the
repository root, build the app:

```sh
pebble build
```

The package is written to `build/astroslop.pbw`.

For a release, update the version in the manifest and lockfile together, without
creating a Git tag:

```sh
npm version 0.1.1 --no-git-tag-version
```

The watch and settings About screens use the version from `package.json`.

The GitHub Actions workflow checks the Pebble appstore after a successful build
and tests on pushes to `main`. If the version is new, it publishes the built
package after any protection rules on the `Pebble Appstore` environment pass.
Create the listing in the Pebble Developer Dashboard first, authorize
`.github/workflows/build.yml` on `main` for GitHub Actions publishing, and set
the environment variable `APPSTORE_APP_ID` to the listing ID. Configure the
environment to allow the `main` branch and require a reviewer if approval is
wanted. Without a reviewer, a new version can be published automatically.

Set the listing's **App Name** to **Telescope PushTo Guide** in the
[Pebble Developer Dashboard](https://developer.repebble.com/dashboard).
The release upload does not rename the listing. The watch launcher label
is **PushTo Guide**, set by `pebble.displayName` in `package.json`.

Pushing a matching `vX.Y.Z` tag separately creates a GitHub Release; tags do not
publish to the Pebble appstore. Versions already returned by the appstore's
developer API are not uploaded again.

### Install on a watch

Pair your watch with the Pebble mobile app and sign in to your Pebble account.
On the **Devices** tab, open the watch's three-dot menu and enable
**Dev Connection**. From the repository root, sign in with the same account
and install the package:

```sh
pebble login  # needed once
pebble install --cloudpebble build/astroslop.pbw
```

Allow the app access to your phone's location when prompted.

### Run in an emulator

```sh
pebble install --emulator emery
```

Use `--vnc` for **all** emulator commands in a headless environment, for
example:

```sh
pebble install --emulator emery --vnc
pebble screenshot --emulator emery --vnc --scale 6 --no-open screenshot.png
pebble emu-button --emulator emery --vnc click select
```

### Run the tests

Run the host tests (Node.js is needed for the companion test):

```sh
make -C test/astro
python3 -m unittest discover -s test/tools -v
node test/pkjs/companion.test.js
```

## Adding a catalog

Create a CSV under `catalogs/` using an existing file as a template. Supply
`title`, `short_title`, `provider`, `source` and `license` header comments and
the provider's columns; register the filename in `catalogs/catalogs.json`.
Fixed-object coordinates are J2000 degrees. IDs, names, titles, short titles
and source descriptions must fit the watch's 31-, 63-, 39-, 23- and 127-byte
UTF-8 limits respectively. The build validates the file and regenerates the
watch resource, About/settings credits and [CREDITS.md](CREDITS.md) from those
headers. Edit catalog headers rather than the generated credits document.

## Limitations and open issues

- GPS fixes without altitude use sea level and show a warning. The weather
  plugin's first saved location may differ from the observer's location;
  readings expire at the provider's `validUntilMs`, or after three hours when
  no expiry is supplied.
- Minor-planet elements cover 2026–2036; accuracy degrades outside the table.
  The results are intended for visual pointing, not precision navigation.
  Reference-vector tests cover sampled dates and locations, not every possible
  observing condition.
- The flint build is close to its 64 KB RAM limit, and resources are about
  121 KB—larger than the design estimates. Emulator filter searches can take
  about a second. Physical-watch performance, battery life, backlight color,
  and real Location-permission behavior still need verification.
- English is the only supplied UI language. The SDK linker emits RWX-segment
  warnings for the watch binaries.

## Design documentation

Read the [Design and As-Built Document 0.2](docs/DESIGN.md) for requirements,
architecture, algorithms, mockups and verification status.
