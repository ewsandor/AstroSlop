'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

async function main() {
  const listeners = {};
  const sent = [];
  const gps = [];
  const subscriptions = [];
  const storage = {};
  let permission = 'granted';
  const opened = [];
  let clay;
  class FakeClay {
    constructor(config, customFn, options) {
      assert.equal(options.autoHandleEvents, false);
      this.config = config;
      this.meta = {userData: {}};
      clay = this;
    }
    setSettings(settings) { this.settings = settings; }
    generateUrl() { return 'data:clay'; }
    getSettings(response, convert) {
      assert.equal(convert, false);
      return JSON.parse(response);
    }
  }
  const context = {
    console: {log() {}},
    require(id) {
      if (id === '@rebble/clay') return FakeClay;
      if (id === './config') {
        return require(path.join(__dirname, '../../src/pkjs/config.js'));
      }
      assert.equal(id, '../../resources/generated/catalog_credits.json');
      return [{title: 'Test catalog', source: 'Test source', license: 'CC0-1.0'}];
    },
    setTimeout,
    localStorage: {
      getItem(key) { return storage[key] || null; },
      setItem(key, value) { storage[key] = value; }
    },
    navigator: {geolocation: {
      getCurrentPosition(success, failure, options) { gps.push({success, failure, options}); }
    }},
    Pebble: {
      addEventListener(name, callback) { listeners[name] = callback; },
      openURL(url) { opened.push(url); },
      getActiveWatchInfo() { return {platform: 'emery'}; },
      sendAppMessage(message, success) { sent.push(message); success(); },
      permissions: {query() { return Promise.resolve(permission); }},
      subscribeToSource(options) {
        const sub = {options, unsubscribed: false, unsubscribe() { this.unsubscribed = true; }};
        subscriptions.push(sub);
        return sub;
      }
    }
  };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../../src/pkjs/index.js'), 'utf8'),
                     context);
  listeners.ready({});
  assert.equal(sent[0].JS_READY, 1);
  assert.equal(sent[1].CFG_FLASHLIGHT_ENABLED, 1);
  listeners.appmessage({payload: {REQ_LOCATION: 2}});
  assert.equal(gps[0].options.maximumAge, 0);
  gps[0].success({coords: {latitude: 51.5, longitude: -0.1, altitude: null, accuracy: 5}});
  let fix = sent.at(-1);
  assert.equal(fix.LOC_LAT_E6, 51500000);
  assert.equal(fix.LOC_LON_E6, -100000);
  assert.equal(fix.LOC_ALT_VALID, 0);
  assert.equal(fix.LOC_ALT_M, 0);

  function page(values) {
    const response = {};
    Object.keys(values).forEach(key => { response[key] = {value: values[key]}; });
    listeners.webviewclosed({response: JSON.stringify(response)});
    return new Promise(resolve => setTimeout(resolve, 0));
  }
  async function open() {
    listeners.showConfiguration({});
    await new Promise(resolve => setTimeout(resolve, 0));
    return clay;
  }
  let shown = await open();
  assert.equal(opened.length, 1);
  assert.equal(shown.settings.CFG_MIN_ELEV, 0);
  assert.equal(shown.settings.CFG_SHOW_TIME, true);
  assert.equal(shown.settings.CFG_LABEL_MODE, '2');
  assert.equal(shown.settings.LATITUDE, '51.4778');
  assert.equal(shown.meta.userData.lastFix.latitude, 51.5);
  const configText = JSON.stringify(shown.config);
  assert.ok(configText.includes('Astronomy Tools ' + require('../../package.json').version + '.'));
  assert.match(configText, /Test source/);
  assert.match(configText, /bending of light by the atmosphere/);
  assert.match(configText, /larger numbers are dimmer/);

  let count = sent.length;
  await page({CFG_MIN_ELEV: 61, CFG_LABEL_MODE: '1', CFG_SHOW_TIME: true, LATITUDE: ''});
  assert.equal(sent.length, count + 1);
  assert.deepEqual({...sent.at(-1)}, {CFG_LABEL_MODE: 1});
  count = sent.length;
  await page({CFG_LABEL_MODE: '1'});
  assert.equal(sent.length, count);

  await page({CFG_USE_WEATHER: true});
  assert.equal(subscriptions.length, 1);
  const expiry = Date.now() + 1800000;
  subscriptions[0].options.onData({
    validUntilMs: expiry,
    instances: [{properties: {temperature: {numericValue: {value: 68, unit: '\u00b0F'}}}}]
  });
  let weather = sent.at(-1);
  assert.equal(weather.WX_TEMP_DC, 200);
  assert.equal(weather.WX_TEMP_UNIT, 1);
  assert.equal(weather.WX_EXPIRES, Math.floor(expiry / 1000));
  listeners.appmessage({payload: {REQ_LOCATION: 1}});
  assert.equal(subscriptions[0].unsubscribed, true);
  assert.equal(subscriptions.length, 2);
  subscriptions[1].options.onData({
    instances: [{properties: {temperature: {numericValue: {value: 10, unit: '\u00b0C'}}}}]
  });
  weather = sent.at(-1);
  assert.ok(weather.WX_EXPIRES >= Math.floor(Date.now() / 1000) + 10799);

  await page({CFG_USE_WEATHER: false});
  permission = 'denied';
  shown = await open();
  assert.match(JSON.stringify(shown.config), /permission is off/);
  count = sent.length;
  await page({CFG_USE_WEATHER: true});
  assert.equal(sent.length, count);
  delete context.Pebble.permissions;
  await page({CFG_USE_WEATHER: true});
  assert.equal(sent.at(-1).CFG_USE_WEATHER, 1);

  await page({MANUAL: true, LATITUDE: '10.5', LONGITUDE: '-20.25', ELEVATION: '100'});
  fix = sent.at(-1);
  assert.equal(fix.LOC_SOURCE, 1);
  assert.equal(fix.LOC_LAT_E6, 10500000);
  assert.equal(fix.LOC_LON_E6, -20250000);
  assert.equal(fix.LOC_ALT_M, 100);
  const pendingGps = gps.at(-1);
  pendingGps.success({coords: {latitude: 0, longitude: 0, altitude: 0, accuracy: 5}});
  assert.equal(sent.at(-1).LOC_SOURCE, 1);
  await page({ELEVATION: '200'});
  assert.equal(sent.at(-1).LOC_ALT_M, 200);
  console.log('PebbleKit JS companion tests passed');
}

main().catch(error => { console.error(error); process.exitCode = 1; });
