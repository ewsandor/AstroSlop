/* Clay settings page layout. Keys without a CFG_ prefix are phone-only settings. */
'use strict';

var appVersion = require('../../package.json').version;
var appName = 'Telescope PushTo Guide';

function options(values) {
  return values.map(function(pair) { return {label: pair[1], value: String(pair[0])}; });
}

function escapeHtml(text) {
  return String(text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function toggle(key, label, description) {
  return {type: 'toggle', messageKey: key, label: label, description: description};
}

/* page: {notice, catalogCredits} supplied when the page opens. */
module.exports = function(page) {
  var credits = (page.catalogCredits || []).map(function(credit) {
    return '<li>' + escapeHtml(credit.title + ': ' + credit.source + ' (' + credit.license + ')') +
      '</li>';
  }).join('');
  var config = [{type: 'heading', defaultValue: appName}];
  if (page.notice) config.push({type: 'text', defaultValue: page.notice});
  return config.concat([
    {type: 'section', items: [
      {type: 'heading', defaultValue: 'Filters'},
      toggle('CFG_MIN_ELEV_ENABLED', 'Skip objects below elevation',
             "Uses each object's current elevation above the horizon."),
      {type: 'slider', messageKey: 'CFG_MIN_ELEV', label: 'Minimum elevation (degrees)',
       description: 'Objects lower than this are skipped when moving through the catalog.',
       min: -10, max: 60, step: 1},
      toggle('CFG_MAX_MAG_ENABLED', 'Limit faintest magnitude',
             'Magnitude is inverse: larger numbers are dimmer.'),
      {type: 'slider', messageKey: 'CFG_MAX_MAG', label: 'Faintest magnitude',
       description: 'Objects dimmer than this limit are skipped when browsing.',
       min: -2, max: 15, step: 1}
    ]},
    {type: 'section', items: [
      {type: 'heading', defaultValue: 'Display'},
      {type: 'select', messageKey: 'CFG_LABEL_MODE', label: 'Object label',
       options: options([[0, 'ID'], [1, 'Name'], [2, 'ID + name']])},
      toggle('CFG_SHOW_TIME', 'Show local time'),
      toggle('CFG_SHOW_EVENT', 'Show next rise or set'),
      toggle('CFG_SHOW_MAG', 'Show magnitude'),
      toggle('CFG_USE_WEATHER', 'Use local weather',
             'Uses temperature from the first location in the Pebble weather plugin for the ' +
             'status bar and refraction correction. Requires plugins to be enabled.'),
      {type: 'select', messageKey: 'CFG_PRECISION', label: 'Coordinate precision',
       description: 'Changes displayed rounding only; it does not improve calculation accuracy.',
       options: options([[0, '0.1 degrees'], [1, '0.01 degrees']])},
      toggle('CFG_BANNER', 'Show object banner',
             'Briefly shows the object name or ID when you switch objects.'),
      toggle('CFG_INVERT', 'Invert colors')
    ]},
    {type: 'section', items: [
      {type: 'heading', defaultValue: 'Backlight'},
      {type: 'toggle', messageKey: 'CFG_TINT', label: 'Red backlight',
       capabilities: ['PLATFORM_EMERY']},
      toggle('CFG_FLASHLIGHT_ENABLED', 'Flashlight on long-press Select',
             'Hold Select to turn on; hold again to turn off.'),
      {type: 'text', capabilities: ['NOT_PLATFORM_EMERY'],
       defaultValue: 'This watch has a white backlight, which can affect night vision.'},
      {type: 'select', messageKey: 'CFG_FLASHLIGHT_MIN', label: 'Flashlight duration',
       options: options([[1, '1 minute'], [2, '2 minutes'], [5, '5 minutes'], [10, '10 minutes']])}
    ]},
    {type: 'section', items: [
      {type: 'heading', defaultValue: 'Location'},
      toggle('MANUAL', 'Use manual location',
             "Uses the coordinates below instead of the phone's GPS fix."),
      {type: 'input', messageKey: 'LATITUDE', label: 'Latitude (degrees, +N)',
       description: 'North is positive; south is negative.',
       attributes: {type: 'number', min: -90, max: 90, step: 'any'}},
      {type: 'input', messageKey: 'LONGITUDE', label: 'Longitude (degrees, +E)',
       description: 'East is positive; west is negative.',
       attributes: {type: 'number', min: -180, max: 180, step: 'any'}},
      {type: 'input', messageKey: 'ELEVATION', label: 'Elevation (meters)',
       description: 'Above sea level; also sets estimated air pressure for refraction.',
       attributes: {type: 'number', min: -500, max: 12000, step: 1}},
      {type: 'button', id: 'last-fix', defaultValue: 'Use last phone GPS fix'},
      {type: 'text', id: 'last-fix-note', defaultValue:
       'Long-press Down on the watch to refresh location. If GPS altitude is unavailable, ' +
       'the watch assumes sea level and shows a warning.'}
    ]},
    {type: 'section', items: [
      {type: 'heading', defaultValue: 'Advanced'},
      toggle('CFG_REFRACTION', 'Atmospheric refraction',
             'Corrects apparent elevation for bending of light by the atmosphere, most noticeable ' +
             'near the horizon. Uses elevation-based pressure and, when local weather is enabled, ' +
             'its temperature; otherwise assumes 10 C.')
    ]},
    {type: 'section', items: [
      {type: 'heading', defaultValue: 'About'},
      {type: 'text', defaultValue:
       '<p>' + appName + ' ' + escapeHtml(appVersion) +
       '. Minor-planet elements cover 2026-2036.</p>' +
       '<p>Algorithms: J. Meeus, Astronomical Algorithms; VSOP87, Bretagnon &amp; Francou / ' +
       'IMCCE; NASA/JPL Horizons and SBDB; Sonia Keys (MIT).</p>' +
       '<p>Icons: Pebble iconography, Apache-2.0.</p><ul>' + credits + '</ul>' +
       '<p>Catalog data are for personal and scientific use only; review each ' +
       'source\'s terms.</p>' +
       '<p>Source and complete credits: https://github.com/ewsandor/AstroSlop</p>'}
    ]},
    {type: 'submit', defaultValue: 'Save'}
  ]);
};
