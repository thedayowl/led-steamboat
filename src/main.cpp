// Steamboat Christmas display - ESP32 firmware for a 4-output WS2811 controller
// (GLEDOPTO GL-C-017WL-D by default; see include/config.h)
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
static uint8_t  s_orders[NUM_CHANNELS];

// ---------------------------------------------------------------- light sensor

static float    s_lightEma = -1;      // smoothed raw reading 0-4095
static bool     s_daylight = false;
static uint32_t s_pendingSince = 0;   // when the level first crossed toward a change

bool sensorPresent() { return LIGHT_SENSOR_PIN >= 0; }
int  lightRaw()      { return s_lightEma < 0 ? 0 : (int)(s_lightEma + 0.5f); }
int  lightLevel()    { return (lightRaw() * 100 + 2047) / 4095; }
bool isDaylight()    { return sensorPresent() && s_daylight; }
bool displayActive() { return g_on && !(g_set.autoDark && isDaylight()); }

static void sensorTick(uint32_t now) {
#if LIGHT_SENSOR_PIN >= 0
  static uint32_t last = 0;
  if (s_lightEma >= 0 && now - last < 250) return;
  last = now;
  int raw = analogRead(LIGHT_SENSOR_PIN);
  if (LIGHT_SENSOR_INVERT) raw = 4095 - raw;
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
#else
  (void)now;
#endif
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
#if RELAY_PIN >= 0
  digitalWrite(RELAY_PIN, (on != RELAY_ACTIVE_LOW) ? HIGH : LOW);
#else
  (void)on;
#endif
}

static void relayTick() {
  static int state = -1;
  int want = (displayActive() || g_test.mode != TEST_OFF) ? 1 : 0;
  if (want != state) { state = want; setRelay(want); }
}

static void buttonTick(uint32_t now) {
#if BUTTON_PIN >= 0
  static bool     lastRaw = true, stable = true, longDone = false;
  static uint32_t changedAt = 0, pressedAt = 0;
  bool raw = digitalRead(BUTTON_PIN);           // HIGH = released (pull-up)
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
#else
  (void)now;
#endif
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
  ArduinoOTA.onStart([]() {
    for (int c = 0; c < NUM_CHANNELS; c++) memset(Leds::buffer(c), 0, g_chan[c].count * 3);
    Leds::show(0, s_orders);
  });
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
  bool connected = WiFi.status() == WL_CONNECTED;
  if (!s_apOn && g_net.staSsid[0] && !connected && now - s_wifiStart > WIFI_CONNECT_TIMEOUT_MS) startSetupAP();
  if ((connected || s_apOn) && !s_netServicesStarted) startNetServices();
}

// ---------------------------------------------------------------- setup / loop

void setup() {
  Serial.setRxBufferSize(4096);   // room for a pasted backup
  Serial.begin(SERIAL_BAUD);
  delay(50);
  Serial.printf("\r\n\r\nSteamboat lights %s starting\r\n", FW_VERSION);

#if RELAY_PIN >= 0
  pinMode(RELAY_PIN, OUTPUT);
  setRelay(false);
#endif
#if BUTTON_PIN >= 0
  pinMode(BUTTON_PIN, INPUT_PULLUP);
#endif
#if LIGHT_SENSOR_PIN >= 0
  analogReadResolution(12);
  analogSetPinAttenuation(LIGHT_SENSOR_PIN, ADC_11db);   // full 0-3.3 V range
#endif

  if (!Leds::begin(LED_PINS, NUM_CHANNELS)) Serial.print("WARNING: an LED output failed to start\r\n");
  loadConfig();                // also sets LED counts and network settings
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

  if (now - s_lastFrame >= FRAME_MS) {
    s_lastFrame = now;
    for (int c = 0; c < NUM_CHANNELS; c++) { s_orders[c] = g_chan[c].order; Leds::setType(c, g_chan[c].type); }
    displayRender(now);
    Leds::show(g_set.brightness, s_orders);
  }
  delay(1);
}
