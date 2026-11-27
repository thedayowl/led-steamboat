// Steamboat Christmas display - ESP32 firmware for WS2811/WS2812 controllers
// (GLEDOPTO GL-C-017WL-D by default; other boards via 'preset' or 'pins')
//
//  * Sections of LEDs on each output are defined in include/config.h and can be
//    remapped at runtime from the serial console or the web page.
//  * Static sections show a solid color; the waterline flows, three smoke puffs
//    rise from the stack, and the paddle wheel spokes turn.
//  * Optional photoresistor turns the display off in daylight.
//  * Web page with backup/restore and firmware upload; ArduinoOTA too.
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include "app.h"
#include "led_driver.h"

static bool     s_apOn = false;
static bool     s_netServicesStarted = false;
static uint32_t s_wifiStart = 0;
static uint32_t s_lastFrame = 0;
static uint8_t  s_orders[MAX_CHANNELS];

// ---------------------------------------------------------------- light sensor

static float    s_lightEma = -1;      // smoothed raw reading 0-4095
static bool     s_daylight = false;
static uint32_t s_pendingSince = 0;   // when the level first crossed toward a change

bool sensorPresent() { return hwRun().sensorPin >= 0; }
int  lightRaw()      { return s_lightEma < 0 ? 0 : (int)(s_lightEma + 0.5f); }
int  lightLevel()    { return (lightRaw() * 100 + 2047) / 4095; }
bool isDaylight()    { return sensorPresent() && s_daylight; }
bool displayActive() { return g_on && !(g_set.autoDark && isDaylight()); }

static void sensorTick(uint32_t now) {
  if (!sensorPresent()) return;
  static uint32_t last = 0;
  if (s_lightEma >= 0 && now - last < 250) return;
  last = now;
  int raw = analogRead(hwRun().sensorPin);
  if (hwRun().sensorInvert) raw = 4095 - raw;
  bool first = s_lightEma < 0;
  if (first) s_lightEma = raw;
  else s_lightEma += 0.08f * (raw - s_lightEma);   // ~3 s smoothing
  int pct = lightLevel();
  if (first) { s_daylight = pct > g_set.dayLevel; return; }   // decide at once on power-up
  // Hysteresis: day -> night below darkLevel, night -> day above dayLevel,
  // and the new state must hold for sensorDelay seconds.
  bool want = s_daylight ? !(pct < g_set.darkLevel) : (pct > g_set.dayLevel);
  if (want == s_daylight) { s_pendingSince = 0; return; }
  if (!s_pendingSince) s_pendingSince = now ? now : 1;
  if (now - s_pendingSince >= (uint32_t)g_set.sensorDelay * 1000UL) { s_daylight = want; s_pendingSince = 0; }
}

void sensorStatus(Print& out) {
  if (!sensorPresent()) { out.print("No light sensor configured.\r\n"); return; }
  out.printf("Light %d%% (raw %d of 4095). Sensor says %s", lightLevel(), lightRaw(), s_daylight ? "DAY" : "NIGHT");
  if (s_pendingSince)
    out.printf(", switching to %s in %lu s", s_daylight ? "night" : "day",
               (unsigned long)(((uint32_t)g_set.sensorDelay * 1000UL - (millis() - s_pendingSince)) / 1000UL));
  out.print(".\r\n");
  out.printf("Daylight auto-off %s: on below %u%%, off above %u%%, after %u s. Display is %s.\r\n",
             g_set.autoDark ? "ENABLED" : "disabled", g_set.darkLevel, g_set.dayLevel, g_set.sensorDelay,
             displayActive() ? "lit" : (g_on ? "dark (daylight)" : "off"));
}

// ---------------------------------------------------------------- relay & button

static void setRelay(bool on) {
  const HwCfg& h = hwRun();
  if (h.relayPin >= 0) digitalWrite(h.relayPin, (on != (bool)h.relayActiveLow) ? HIGH : LOW);
}

static void relayTick() {
  static int state = -1;
  int want = (displayActive() || g_test.mode != TEST_OFF) ? 1 : 0;
  if (want != state) { state = want; setRelay(want); }
}

static void buttonTick(uint32_t now) {
  if (hwRun().buttonPin < 0) return;
  static bool     lastRaw = true, stable = true, longDone = false;
  static uint32_t changedAt = 0, pressedAt = 0;
  bool raw = digitalRead(hwRun().buttonPin);    // HIGH = released (pull-up)
  if (raw != lastRaw) { lastRaw = raw; changedAt = now; }
  if (now - changedAt < 30) return;             // debounce
  if (raw != stable) {
    stable = raw;
    if (!stable) { pressedAt = now; longDone = false; }          // pressed
    else if (!longDone && now - pressedAt < 2000) g_on = !g_on;  // short press released
  }
  if (!stable && !longDone && now - pressedAt >= 5000) {         // held 5 s
    longDone = true;
    startSetupAP();
  }
}

// ---------------------------------------------------------------- network

String netInfo() {
  String s;
  if (WiFi.status() == WL_CONNECTED)
    s = "Wi-Fi " + WiFi.SSID() + ", http://" + WiFi.localIP().toString() + " (http://" HOSTNAME ".local)";
  else if (g_net.staSsid[0]) s = String("Wi-Fi: not connected to ") + g_net.staSsid;
  else s = "Wi-Fi: no network configured";
  if (s_apOn) s += String(", setup AP '") + g_net.apSsid + "' at http://" + WiFi.softAPIP().toString();
  return s;
}

void startSetupAP() {
  if (s_apOn) return;
  WiFi.mode(g_net.staSsid[0] ? WIFI_AP_STA : WIFI_AP);
  // WPA2-PSK needs 8-63 characters; never fall back to an open AP.
  size_t len = strlen(g_net.apPass);
  const char* pass = (len >= 8 && len <= 63) ? g_net.apPass : AP_PASSWORD;
  const char* ssid = g_net.apSsid[0] ? g_net.apSsid : AP_SSID;
  WiFi.softAP(ssid, pass);
  s_apOn = true;
}

static void startNetServices() {
  if (s_netServicesStarted) return;
  s_netServicesStarted = true;
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(g_net.adminPass);
  ArduinoOTA.onStart([]() { ledsBlank(); });
  ArduinoOTA.begin();          // also starts mDNS (steamboat.local)
  MDNS.addService("http", "tcp", 80);
}

static void wifiBegin() {
  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);
  if (g_net.staSsid[0]) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);      // steadier LED timing and web response
    WiFi.setAutoReconnect(true);
    WiFi.begin(g_net.staSsid, g_net.staPass);
  } else {
    startSetupAP();
  }
  s_wifiStart = millis();
}

static void wifiTick(uint32_t now) {
  static uint32_t lastRetry = 0;
  bool connected = WiFi.status() == WL_CONNECTED;
  if (!s_apOn && g_net.staSsid[0] && !connected && now - s_wifiStart > WIFI_CONNECT_TIMEOUT_MS) { startSetupAP(); lastRetry = now; }
  // The core's auto-reconnect gives up after some failures (e.g. a WPA
  // handshake timeout), which would leave the board on its setup AP for good.
  // Keep trying the home network every minute; the AP stays up meanwhile.
  if (s_apOn && g_net.staSsid[0] && !connected && now - lastRetry > 60000) {
    lastRetry = now;
    WiFi.begin(g_net.staSsid, g_net.staPass);
  }
  if ((connected || s_apOn) && !s_netServicesStarted) startNetServices();
}

// ---------------------------------------------------------------- setup / loop

void setup() {
  Serial.setRxBufferSize(4096);   // room for a pasted backup
  Serial.begin(SERIAL_BAUD);
  delay(50);
  Serial.printf("\r\n\r\nSteamboat lights %s starting\r\n", FW_VERSION);

  loadConfig();                // show, sections, network and pin settings
  migrateIfNeeded(Serial);     // first boot after installing over WLED: fix the partition table, restart
  hwStart();                   // LED outputs from the saved pin settings (conflicts left off)
  const HwCfg& hw = hwRun();
  if (hw.relayPin >= 0) { pinMode(hw.relayPin, OUTPUT); setRelay(false); }
  if (hw.buttonPin >= 0) pinMode(hw.buttonPin, hw.buttonPin >= 34 ? INPUT : INPUT_PULLUP);   // 34-39: board has its own pull-up
  if (hw.sensorPin >= 0) {
    analogReadResolution(12);
    analogSetPinAttenuation(hw.sensorPin, ADC_11db);   // full 0-3.3 V range
  }
  // The connector pin jumpered to the sensor pin: floating input, no pulls.
  int jp = SENSOR_JUMPER_PIN;
  bool jpUsed = jp == hw.relayPin || jp == hw.buttonPin || jp == hw.sensorPin;
  for (int c = 0; c < hw.numCh; c++) if (hw.ledPin[c] == jp) jpUsed = true;
  if (jp >= 0 && !jpUsed && inputPinOk(jp)) pinMode(jp, INPUT);
  if (validate(Serial, false)) Serial.print("Section table has errors; type 'check' in the console.\r\n");
  sensorTick(millis());        // first reading decides day/night immediately
  consoleBegin();              // after this, serial output may be muted if GPIO1 drives LEDs
  wifiBegin();
  webBegin();
}

void loop() {
  uint32_t now = millis();
  consolePoll();
  webLoop();
  if (s_netServicesStarted) ArduinoOTA.handle();
  wifiTick(now);
  buttonTick(now);
  sensorTick(now);
  consoleTick(now);
  relayTick();

  // While a GitHub update downloads, send one dark frame and then leave the
  // LEDs alone, so flash writes can't disturb the output.
  static bool blanked = false;
  if (ghBusy()) {
    if (!blanked) { blanked = true; ledsBlank(); }
  } else if (now - s_lastFrame >= FRAME_MS) {
    blanked = false;
    s_lastFrame = now;
    for (int c = 0; c < numChannels(); c++) { s_orders[c] = g_chan[c].order; Leds::setType(c, g_chan[c].type); }
    displayRender(now);
    Leds::show(g_set.brightness, s_orders);
  }
  delay(1);
}
