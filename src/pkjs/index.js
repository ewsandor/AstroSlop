/* PebbleKit JS: location, optional weather, and the Clay settings page. */
'use strict';

var Clay = require('@rebble/clay');
var buildConfig = require('./config');
var catalogCredits = require('../../resources/generated/catalog_credits.json');

var SETTINGS_KEY = 'astroslop.settings.v1';
var LAST_FIX_KEY = 'astroslop.lastGpsFix.v1';
var WATCH_KEYS = {
  labelMode: 'CFG_LABEL_MODE', showTime: 'CFG_SHOW_TIME',
  showEvent: 'CFG_SHOW_EVENT', showMagnitude: 'CFG_SHOW_MAG',
  useWeather: 'CFG_USE_WEATHER', precision: 'CFG_PRECISION',
  banner: 'CFG_BANNER', inverted: 'CFG_INVERT',
  minElevationEnabled: 'CFG_MIN_ELEV_ENABLED', minElevation: 'CFG_MIN_ELEV',
  maxMagnitudeEnabled: 'CFG_MAX_MAG_ENABLED', maxMagnitude: 'CFG_MAX_MAG',
  refraction: 'CFG_REFRACTION', redTint: 'CFG_TINT',
  flashlightEnabled: 'CFG_FLASHLIGHT_ENABLED', flashlightMinutes: 'CFG_FLASHLIGHT_MIN'
};
var PHONE_KEYS = {manual: 'MANUAL', latitude: 'LATITUDE', longitude: 'LONGITUDE', elevation: 'ELEVATION'};
var LOCATION_SETTINGS = ['latitude', 'longitude', 'elevation'];
var BOOLEAN_SETTINGS = [
  'showTime', 'showEvent', 'showMagnitude', 'useWeather', 'precision', 'banner',
  'inverted', 'minElevationEnabled', 'maxMagnitudeEnabled', 'refraction', 'redTint',
  'flashlightEnabled', 'manual'
];
var ALLOWED = {
  labelMode: [0, 1, 2], flashlightMinutes: [1, 2, 5, 10]
};
var RANGES = {
  minElevation: [-10, 60], maxMagnitude: [-2, 15],
  latitude: [-90, 90], longitude: [-180, 180], elevation: [-500, 12000]
};
var settings;
var lastFix = null;
var platform = '';
var subscription = null;
var requestGeneration = 0;
var queue = [];
var sending = false;

function defaults() {
  return {
    labelMode: 2, showTime: 1, showEvent: 1, showMagnitude: 1, useWeather: 0,
    precision: 0, banner: 1, inverted: 0,
    minElevationEnabled: 1, minElevation: 0, maxMagnitudeEnabled: 0, maxMagnitude: 8,
    refraction: 1, redTint: 1, flashlightEnabled: platform === 'emery' ? 1 : 0,
    flashlightMinutes: 5, manual: 0, latitude: 51.4778, longitude: -0.0015, elevation: 46
  };
}

function validSetting(name, value) {
  if (BOOLEAN_SETTINGS.indexOf(name) !== -1) return value === 0 || value === 1;
  if (ALLOWED[name]) return ALLOWED[name].indexOf(value) !== -1;
  if (RANGES[name]) {
    return typeof value === 'number' && isFinite(value) &&
      value >= RANGES[name][0] && value <= RANGES[name][1] &&
      (name === 'latitude' || name === 'longitude' || Math.floor(value) === value);
  }
  return false;
}

function readStorage(key) {
  var text = localStorage.getItem(key);
  if (!text) return null;
  try {
    return JSON.parse(text);
  } catch (error) {
    console.log('Invalid saved ' + key + ': ' + error.message);
    return null;
  }
}

function loadSettings() {
  var result = defaults();
  var stored = readStorage(SETTINGS_KEY);
  if (stored && typeof stored === 'object') {
    Object.keys(result).forEach(function(name) {
      if (Object.prototype.hasOwnProperty.call(stored, name)) {
        if (validSetting(name, stored[name])) result[name] = stored[name];
        else console.log('Ignoring invalid saved setting: ' + name);
      }
    });
  }
  return result;
}

function saveSettings() {
  localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
}

function pump() {
  if (sending || queue.length === 0) return;
  sending = true;
  var item = queue[0];
  Pebble.sendAppMessage(item.message, function() {
    queue.shift();
    sending = false;
    pump();
  }, function(error) {
    sending = false;
    item.attempts += 1;
    console.log('AppMessage failed: ' + JSON.stringify(error));
    if (item.attempts >= 3 && !item.ready) {
      queue.shift();
      pump();
    } else {
      setTimeout(pump, item.ready ? 10000 : 1000 * item.attempts);
    }
  });
}

function send(message, ready) {
  queue.push({message: message, attempts: 0, ready: !!ready});
  pump();
}

function sendSettings() {
  var message = {};
  Object.keys(WATCH_KEYS).forEach(function(name) { message[WATCH_KEYS[name]] = settings[name]; });
  send(message);
}

function permissionState() {
  if (!Pebble.permissions || typeof Pebble.permissions.query !== 'function') {
    console.log('Location permission API unavailable');
    return Promise.resolve(typeof Pebble.subscribeToSource === 'function' ? 'unknown' : 'unavailable');
  }
  return Pebble.permissions.query('Location').catch(function(error) {
    console.log('Could not query Location permission: ' + error);
    return 'unavailable';
  });
}

function clearWeather() {
  send({WX_CLEAR: 1});
}

function errorDescription(error) {
  if (!error) return 'unknown error';
  return (error.code === undefined ? 'unknown' : error.code) +
    (error.message ? ' ' + error.message : '');
}

function unsubscribeWeather() {
  if (subscription) {
    subscription.unsubscribe();
    subscription = null;
  }
}

function subscribeWeather() {
  unsubscribeWeather();
  if (!settings.useWeather) {
    clearWeather();
    return;
  }
  if (typeof Pebble.subscribeToSource !== 'function') {
    console.log('Weather plugin API unavailable');
    clearWeather();
    return;
  }
  subscription = Pebble.subscribeToSource({
    category: 'weather',
    item: 'location',
    properties: ['temperature'],
    onData: function(update) {
      var first = update.instances && update.instances[0];
      var reading = first && first.properties && first.properties.temperature &&
        first.properties.temperature.numericValue;
      if (!reading || typeof reading.value !== 'number' || !isFinite(reading.value) ||
          (reading.unit !== '\u00b0C' && reading.unit !== '\u00b0F')) {
        console.log('Weather update has no usable temperature');
        clearWeather();
        return;
      }
      var fahrenheit = reading.unit === '\u00b0F';
      var celsius = fahrenheit ? (reading.value - 32) * 5 / 9 : reading.value;
      if (celsius < -100 || celsius > 80) {
        console.log('Weather temperature is outside supported range');
        clearWeather();
        return;
      }
      var expires = typeof update.validUntilMs === 'number' &&
        isFinite(update.validUntilMs) ? update.validUntilMs : Date.now() + 3 * 60 * 60 * 1000;
      if (expires <= Date.now()) {
        console.log('Weather update has already expired');
        clearWeather();
        return;
      }
      send({
        WX_TEMP_DC: Math.round(celsius * 10),
        WX_TEMP_UNIT: fahrenheit ? 1 : 0,
        WX_EXPIRES: Math.floor(expires / 1000)
      });
    },
    onError: function(error) {
      console.log('Weather unavailable: ' + errorDescription(error));
      clearWeather();
    }
  });
}

function sendFix(fix, source) {
  send({
    LOC_LAT_E6: Math.round(fix.latitude * 1000000),
    LOC_LON_E6: Math.round(fix.longitude * 1000000),
    LOC_ALT_M: Math.round(fix.elevation),
    LOC_ALT_VALID: fix.altitudeKnown ? 1 : 0,
    LOC_ACC_M: source ? 0 : fix.accuracy,
    LOC_SOURCE: source,
    LOC_TIME: Math.floor(Date.now() / 1000)
  });
}

function manualFix() {
  return {
    latitude: settings.latitude, longitude: settings.longitude,
    elevation: settings.elevation, altitudeKnown: true, accuracy: 0
  };
}

function requestLocation(forced) {
  var generation = ++requestGeneration;
  subscribeWeather();
  if (settings.manual) {
    sendFix(manualFix(), 1);
    return;
  }
  if (!navigator.geolocation || typeof navigator.geolocation.getCurrentPosition !== 'function') {
    console.log('Phone geolocation is unavailable');
    send({LOC_ERROR: 3});
    return;
  }
  navigator.geolocation.getCurrentPosition(function(position) {
    if (generation !== requestGeneration) return;
    var coords = position.coords || {};
    if (typeof coords.latitude !== 'number' || !isFinite(coords.latitude) ||
        typeof coords.longitude !== 'number' || !isFinite(coords.longitude) ||
        coords.latitude < -90 || coords.latitude > 90 ||
        coords.longitude < -180 || coords.longitude > 180) {
      console.log('Phone supplied invalid GPS coordinates');
      send({LOC_ERROR: 3});
      return;
    }
    var altitudeKnown = typeof coords.altitude === 'number' && isFinite(coords.altitude) &&
      coords.altitude >= -500 && coords.altitude <= 12000;
    if (!altitudeKnown) console.log('GPS altitude unavailable; watch will use sea level');
    var accuracy = typeof coords.accuracy === 'number' && isFinite(coords.accuracy) &&
      coords.accuracy >= 0 ? Math.min(Math.round(coords.accuracy), 2147483647) : 2147483647;
    var fix = {
      latitude: coords.latitude, longitude: coords.longitude,
      elevation: altitudeKnown ? coords.altitude : 0,
      altitudeKnown: altitudeKnown, accuracy: accuracy
    };
    lastFix = fix;
    localStorage.setItem(LAST_FIX_KEY, JSON.stringify(fix));
    sendFix(fix, 0);
  }, function(error) {
    if (generation !== requestGeneration) return;
    console.log('Phone geolocation failed: ' + errorDescription(error));
    permissionState().then(function(state) {
      if (generation !== requestGeneration) return;
      send({LOC_ERROR: state === 'denied' ? 2 : state === 'prompt' ? 1 : 3});
    });
  }, {enableHighAccuracy: true, timeout: 30000, maximumAge: forced ? 0 : 60000});
}

function pageKey(name) {
  return WATCH_KEYS[name] || PHONE_KEYS[name];
}

/* Clay toggles hold booleans, selects and inputs hold strings, sliders hold numbers. */
function toPage(name) {
  var value = settings[name];
  if (BOOLEAN_SETTINGS.indexOf(name) !== -1 && name !== 'precision') return value === 1;
  if (name === 'minElevation' || name === 'maxMagnitude') return value;
  return String(value);
}

function fromPage(value) {
  if (typeof value === 'boolean') return value ? 1 : 0;
  if (typeof value === 'string') return value.trim() === '' ? NaN : Number(value);
  return value;
}

/* Runs inside the Clay page; it cannot reference anything outside its own body. */
function settingsPageScript() {
  var clayConfig = this;
  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    var fix = clayConfig.meta.userData.lastFix;
    var button = clayConfig.getItemById('last-fix');
    if (!fix) {
      button.disable();
      return;
    }
    button.on('click', function() {
      clayConfig.getItemByMessageKey('LATITUDE').set(String(fix.latitude));
      clayConfig.getItemByMessageKey('LONGITUDE').set(String(fix.longitude));
      if (fix.altitudeKnown) {
        clayConfig.getItemByMessageKey('ELEVATION').set(String(Math.round(fix.elevation)));
      } else {
        clayConfig.getItemById('last-fix-note')
          .set('GPS altitude unavailable; elevation was not changed.');
      }
    });
  });
}

var clay = new Clay(buildConfig({catalogCredits: catalogCredits}), settingsPageScript,
                    {autoHandleEvents: false});

function permissionNotice(state) {
  if (state === 'denied') return 'Location permission is off for this app; GPS and weather are unavailable.';
  if (state === 'prompt') return 'Allow Location in the Pebble app if prompted.';
  return '';
}

function showSettings() {
  var timeout = new Promise(function(resolve) { setTimeout(resolve, 2000, 'unknown'); });
  Promise.race([permissionState(), timeout]).then(function(state) {
    var stored = {};
    Object.keys(settings).forEach(function(name) {
      if (pageKey(name)) stored[pageKey(name)] = toPage(name);
    });
    clay.config = buildConfig({notice: permissionNotice(state), catalogCredits: catalogCredits});
    clay.meta.userData = {lastFix: lastFix};
    clay.setSettings(stored);
    Pebble.openURL(clay.generateUrl());
  });
}

function applyChanges(changes) {
  var names = Object.keys(changes);
  if (names.length === 0) return;
  var message = {};
  names.forEach(function(name) {
    settings[name] = changes[name];
    if (WATCH_KEYS[name]) message[WATCH_KEYS[name]] = changes[name];
  });
  saveSettings();
  if (Object.keys(message).length) send(message);
  var moved = names.some(function(name) { return LOCATION_SETTINGS.indexOf(name) !== -1; });
  if (names.indexOf('manual') !== -1) {
    requestLocation(true);
  } else {
    if (settings.manual && moved) {
      requestGeneration += 1;
      sendFix(manualFix(), 1);
    }
    if (names.indexOf('useWeather') !== -1) subscribeWeather();
  }
}

function saveSettingsPage(response) {
  var values;
  try {
    values = clay.getSettings(response, false);
  } catch (error) {
    console.log('Ignoring settings page response: ' + error.message);
    return;
  }
  var changes = {};
  Object.keys(settings).forEach(function(name) {
    var entry = values[pageKey(name)];
    if (!entry || typeof entry !== 'object') return;
    var value = fromPage(entry.value);
    if (!validSetting(name, value)) console.log('Ignoring invalid setting: ' + name);
    else if (value !== settings[name]) changes[name] = value;
  });
  if (changes.useWeather !== 1) {
    applyChanges(changes);
    return;
  }
  permissionState().then(function(state) {
    if (state === 'denied' || state === 'unavailable') {
      console.log('Weather not enabled; Location permission is ' + state);
      delete changes.useWeather;
    }
    applyChanges(changes);
  });
}

Pebble.addEventListener('ready', function() {
  var watch = Pebble.getActiveWatchInfo();
  platform = watch && watch.platform ? watch.platform : '';
  settings = loadSettings();
  lastFix = readStorage(LAST_FIX_KEY);
  send({JS_READY: 1}, true);
  sendSettings();
  if (settings.useWeather) subscribeWeather();
  console.log('AstroSlop phone companion ready');
});

Pebble.addEventListener('appmessage', function(event) {
  if (event.payload && event.payload.REQ_LOCATION) {
    requestLocation(event.payload.REQ_LOCATION === 2);
  }
});

Pebble.addEventListener('showConfiguration', showSettings);

Pebble.addEventListener('webviewclosed', function(event) {
  if (event && event.response) saveSettingsPage(event.response);
});
