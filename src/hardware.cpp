// hardware.cpp - channel count, LED output GPIOs, light sensor, relay and
// button GPIOs, and presets for common WLED controller boards.
//
// These are saved like the other settings but only read at boot: the RMT
// outputs and the ADC are set up once in hwStart(). g_hw is what the console
// and web page edit; hwRun() is what is actually running.
#include "app.h"
#include "led_driver.h"

static_assert(MAX_CHANNELS >= 1 && MAX_CHANNELS <= 8, "ESP32 RMT has 8 transmit channels");
static_assert(DEFAULT_CHANNELS >= 1 && DEFAULT_CHANNELS <= MAX_CHANNELS, "DEFAULT_CHANNELS must be 1-MAX_CHANNELS");

HwCfg g_hw;
static HwCfg s_boot;   // g_hw as it was at boot, to tell whether changes are waiting for a restart
static HwCfg s_run;    // what started: conflicting pins removed

void hwDefaults(HwCfg& h) {
  memset(&h, 0, sizeof(h));
  h.numCh = DEFAULT_CHANNELS;
  for (int c = 0; c < MAX_CHANNELS; c++) h.ledPin[c] = c < DEFAULT_CHANNELS ? DEFAULT_LED_PINS[c] : -1;
  h.sensorPin = DEFAULT_SENSOR_PIN;
  h.sensorInvert = DEFAULT_SENSOR_INVERT ? 1 : 0;
  h.relayPin = DEFAULT_RELAY_PIN;
  h.relayActiveLow = DEFAULT_RELAY_ACTIVE_LOW ? 1 : 0;
  h.buttonPin = DEFAULT_BUTTON_PIN;
}

void hwSanitize(HwCfg& h) {
  if (h.numCh < 1 || h.numCh > MAX_CHANNELS) h.numCh = DEFAULT_CHANNELS;
  for (int c = 0; c < MAX_CHANNELS; c++) if (h.ledPin[c] < -1 || h.ledPin[c] > 39) h.ledPin[c] = -1;
  if (h.sensorPin < -1 || h.sensorPin > 39) h.sensorPin = -1;
  h.sensorInvert = h.sensorInvert ? 1 : 0;
  if (h.relayPin < -1 || h.relayPin > 39) h.relayPin = -1;
  h.relayActiveLow = h.relayActiveLow ? 1 : 0;
  if (h.buttonPin < -1 || h.buttonPin > 39) h.buttonPin = -1;
}

// ESP32 (the only target this firmware builds for).
bool ledPinOk(int p) {
  if (p < 0 || p > 33) return false;                               // 34-39 are input only
  if (p >= 6 && p <= 11) return false;                             // SPI flash
  if (p == 20 || p == 24 || (p >= 28 && p <= 31)) return false;    // no such pad
  if ((p == 16 || p == 17) && psramFound()) return false;          // PSRAM on WROVER modules
  return true;
}

// ADC1 only: ADC2 can't be read while Wi-Fi is on.
bool sensorPinOk(int p) { return p >= 32 && p <= 39; }

// Any pad except the flash pins. 34-39 have no internal pull-up, so a button
// there needs a resistor to 3.3 V.
bool inputPinOk(int p) {
  if (p < 0 || p > 39) return false;
  if (p >= 6 && p <= 11) return false;
  if (p == 20 || p == 24 || (p >= 28 && p <= 31)) return false;
  if ((p == 16 || p == 17) && psramFound()) return false;
  return true;
}

int hwCheck(const HwCfg& in, HwCfg* use, Print* out, const char* prefix) {
  HwCfg h = in;
  hwSanitize(h);
  int n = 0;
  char why[48], b[140];
  auto say = [&]() { n++; if (out) { out->print(prefix); out->print(b); out->print("\r\n"); } };

  // Relay and button first: LED outputs and the sensor can't take their pins.
  if (h.relayPin >= 0 && !ledPinOk(h.relayPin)) {
    snprintf(b, sizeof(b), "relay: GPIO%d can't be an output, so the relay is off", h.relayPin); say(); h.relayPin = -1;
  }
  if (h.buttonPin >= 0) {
    why[0] = 0;
    if (!inputPinOk(h.buttonPin)) snprintf(why, sizeof(why), "can't be used");
    else if (h.buttonPin == h.relayPin) snprintf(why, sizeof(why), "is the relay pin");
    if (why[0]) { snprintf(b, sizeof(b), "button: GPIO%d %s, so the button is off", h.buttonPin, why); say(); h.buttonPin = -1; }
  }

  for (int c = 0; c < MAX_CHANNELS; c++) {
    if (c >= h.numCh) { h.ledPin[c] = -1; continue; }   // kept in g_hw for later, not started
    int p = h.ledPin[c];
    if (p < 0) { snprintf(b, sizeof(b), "channel %d has no GPIO, so it is off ('chan %d pin <gpio>')", c + 1, c + 1); say(); continue; }
    why[0] = 0;
    if (!ledPinOk(p)) snprintf(why, sizeof(why), "can't drive LEDs");
    else if (p == h.relayPin) snprintf(why, sizeof(why), "is the relay pin");
    else if (p == h.buttonPin) snprintf(why, sizeof(why), "is the button pin");
    else for (int k = 0; k < c; k++)
      if (h.ledPin[k] == p) { snprintf(why, sizeof(why), "is already channel %d's output", k + 1); break; }
    if (why[0]) { snprintf(b, sizeof(b), "channel %d: GPIO%d %s, so channel %d is off", c + 1, p, why, c + 1); say(); h.ledPin[c] = -1; }
  }

  int sp = h.sensorPin;
  if (sp >= 0) {
    why[0] = 0;
    if (!sensorPinOk(sp)) snprintf(why, sizeof(why), "is not an ADC1 pin (use GPIO32-39)");
    else if (sp == h.relayPin) snprintf(why, sizeof(why), "is the relay pin");
    else if (sp == h.buttonPin) snprintf(why, sizeof(why), "is the button pin");
    else for (int c = 0; c < h.numCh; c++)
      if (h.ledPin[c] == sp) { snprintf(why, sizeof(why), "is channel %d's output", c + 1); break; }
    if (why[0]) { snprintf(b, sizeof(b), "light sensor: GPIO%d %s, so the sensor is off", sp, why); say(); h.sensorPin = -1; }
  }
  if (use) *use = h;
  return n;
}

void hwStart() {
  s_boot = g_hw;
  if (hwCheck(g_hw, &s_run, &Serial)) Serial.print("Pin settings have problems (above); type 'pins' in the console.\r\n");
  if (!Leds::begin(s_run.ledPin, s_run.numCh)) Serial.print("WARNING: an LED output failed to start\r\n");
  for (int c = 0; c < Leds::numChannels(); c++) {
    Leds::setCount(c, g_chan[c].count);
    Leds::setType(c, g_chan[c].type);
  }
}

const HwCfg& hwRun() { return s_run; }
const HwCfg& hwBoot() { return s_boot; }
uint8_t numChannels() { return Leds::numChannels(); }

bool hwPending() {
  if (g_hw.numCh != s_boot.numCh || g_hw.sensorPin != s_boot.sensorPin) return true;
  if (g_hw.sensorPin >= 0 && g_hw.sensorInvert != s_boot.sensorInvert) return true;
  if (g_hw.relayPin != s_boot.relayPin || g_hw.buttonPin != s_boot.buttonPin) return true;
  if (g_hw.relayPin >= 0 && g_hw.relayActiveLow != s_boot.relayActiveLow) return true;
  for (int c = 0; c < g_hw.numCh; c++) if (g_hw.ledPin[c] != s_boot.ledPin[c]) return true;
  return false;
}

static void printPin(Print& out, int p) { if (p >= 0) out.printf("GPIO%d", p); else out.print("none"); }

void hwReport(Print& out) {
  out.printf("Channels: %u", g_hw.numCh);
  if (g_hw.numCh != numChannels()) out.printf(" after restart (running %u)", numChannels());
  out.print("\r\n");
  for (int c = 0; c < g_hw.numCh; c++) {
    out.printf("  ch%d  ", c + 1);
    printPin(out, g_hw.ledPin[c]);
    if (c >= numChannels()) out.print("   (starts after restart)");
    else if (g_hw.ledPin[c] != s_boot.ledPin[c]) { out.print("   (after restart; now "); printPin(out, Leds::pin(c)); out.print(")"); }
    else if (g_hw.ledPin[c] >= 0 && !Leds::ok(c)) out.print("   (off)");
    out.print("\r\n");
  }
  out.print("Light sensor: ");
  printPin(out, g_hw.sensorPin);
  if (g_hw.sensorPin >= 0) out.print(g_hw.sensorInvert ? ", inverted" : ", normal");
  if (g_hw.sensorPin != s_boot.sensorPin || (g_hw.sensorPin >= 0 && g_hw.sensorInvert != s_boot.sensorInvert)) {
    out.print("   (after restart; now ");
    printPin(out, s_run.sensorPin);
    out.print(")");
  }
  out.print("\r\n");
  out.print("Relay: ");
  printPin(out, g_hw.relayPin);
  if (g_hw.relayPin >= 0) out.print(g_hw.relayActiveLow ? ", active low" : ", active high");
  if (g_hw.relayPin != s_boot.relayPin || (g_hw.relayPin >= 0 && g_hw.relayActiveLow != s_boot.relayActiveLow)) {
    out.print("   (after restart; now ");
    printPin(out, s_run.relayPin);
    out.print(")");
  }
  out.print("\r\nButton: ");
  printPin(out, g_hw.buttonPin);
  if (g_hw.buttonPin != s_boot.buttonPin) { out.print("   (after restart; now "); printPin(out, s_run.buttonPin); out.print(")"); }
  out.print("\r\n");
  out.print("LED outputs can use GPIO");
  for (int p = 0; p < 40; p++) if (ledPinOk(p)) out.printf(" %d", p);
  out.print("\r\nThe light sensor can use GPIO32-39 (ADC1).\r\n");
  if (hwCheck(g_hw, NULL, NULL)) { out.print("Problems:\r\n"); hwCheck(g_hw, NULL, &out); }
  if (hwPending()) out.print("Changes take effect after 'save' and 'reboot'.\r\n");
}

// ---------------------------------------------------------------- presets
// Pin maps of common ESP32 WLED controllers, from the makers' pinout guides
// and manuals (QuinLED: quinled.info pinout guides and WLED platform.ini
// settings; Gledopto: product manuals; Athom: athom.tech product pages;
// generic: WLED's own defaults). A preset sets the channel count, LED output
// pins, relay and button; the light sensor is left alone (it is an add-on).
// LED pins are in terminal order. ESP32-C3/S3 boards are not listed: this
// firmware is for the original ESP32.
struct Preset {
  const char* name;      // one word, for 'preset <name>'
  const char* title;
  uint8_t     numCh;
  int8_t      pins[MAX_CHANNELS];
  int8_t      relay;
  bool        relayLow;
  int8_t      button;
  const char* note;      // what the maker doesn't publish, or "" 
};

static const Preset kPresets[] = {
  // name          title                                        ch  LED pins                        relay low    button
  { "generic",     "Generic ESP32 board (WLED defaults)",        1, { 16 },                          -1, false,  0, "" },
  { "gledopto017", "Gledopto GL-C-017WL-D",                      4, { 16, 12, 4, 2 },                18, true,  17, "" },
  { "gledopto618", "Gledopto GL-C-618WL",                        4, { 16, 12, 4, 2 },                18, true,  17, "Ethernet port not used" },
  { "gledopto616", "Gledopto GL-C-616WL",                        2, { 16, 2 },                       18, true,  17, "Ethernet port not used" },
  { "gledopto015", "Gledopto GL-C-015WL / 015WL-D",              2, { 16, 2 },                       12, false, -1, "button GPIO not published" },
  { "dig2go",      "QuinLED Dig2Go",                             1, { 16 },                          12, false,  0, "" },
  { "diguno3",     "QuinLED Dig-Uno v3",                         2, { 16, 3 },                       15, false,  0, "relay on Q1" },
  { "digquad3",    "QuinLED Dig-Quad v3",                        4, { 16, 3, 1, 4 },                 15, false,  0, "relay on Q1R" },
  { "diguno4",     "QuinLED Dig-Uno v4",                         2, { 2, 4 },                        -1, false, 37, "relay GPIO not published; set it with 'relay pin'" },
  { "digquad4",    "QuinLED Dig-Quad v4",                        4, { 2, 4, 12, 14 },                -1, false, 37, "relay GPIO not published; set it with 'relay pin'" },
  { "digocta2",    "QuinLED Dig-Octa, Brainboard-32-8L v2",      8, { 0, 1, 2, 3, 4, 5, 12, 13 },    33, false, 34, "" },
  { "digocta4",    "QuinLED Dig-Octa, Brainboard-32-8L v4",      8, { 1, 2, 3, 4, 5, 12, 13, 14 },   33, false, 34, "relay polarity not published" },
  { "athommusic",  "Athom ESP32 music controller",               2, { 18, 17 },                       2, false,  0, "relay polarity not published" },
  { "athompd",     "Athom PD energy-meter controller",           4, { 18, 5, 16, 17 },               -1, false,  0, "" },
  { "athomdmx",    "Athom Ethernet DMX controller V2",           4, { 5, 16, 4, 12 },                 2, false,  0, "relay polarity not published; Ethernet and DMX not used" },
};
static const int kNumPresets = sizeof(kPresets) / sizeof(kPresets[0]);

int         hwPresetCount()        { return kNumPresets; }
const char* hwPresetName(int i)    { return i >= 0 && i < kNumPresets ? kPresets[i].name : ""; }
const char* hwPresetTitle(int i)   { return i >= 0 && i < kNumPresets ? kPresets[i].title : ""; }
const char* hwPresetNote(int i)    { return i >= 0 && i < kNumPresets ? kPresets[i].note : ""; }

int hwFindPreset(const String& name) {
  String w; for (size_t i = 0; i < name.length(); i++) if (isalnum((unsigned char)name[i])) w += (char)tolower((unsigned char)name[i]);
  if (!w.length()) return -1;
  for (int i = 0; i < kNumPresets; i++) if (w == kPresets[i].name) return i;
  int hit = -1, hits = 0;   // unique prefix
  for (int i = 0; i < kNumPresets; i++) if (String(kPresets[i].name).startsWith(w)) { hit = i; hits++; }
  return hits == 1 ? hit : -1;
}

void hwApplyPreset(int i, HwCfg& h) {
  if (i < 0 || i >= kNumPresets) return;
  const Preset& p = kPresets[i];
  h.numCh = p.numCh;
  for (int c = 0; c < MAX_CHANNELS; c++) h.ledPin[c] = c < p.numCh ? p.pins[c] : -1;
  h.relayPin = p.relay;
  h.relayActiveLow = p.relayLow ? 1 : 0;
  h.buttonPin = p.button;
  // The sensor is kept unless the board uses its pin for something else.
  bool clash = h.sensorPin >= 0 && (h.sensorPin == p.relay || h.sensorPin == p.button);
  for (int c = 0; c < p.numCh; c++) if (h.sensorPin == p.pins[c]) clash = true;
  if (clash) h.sensorPin = -1;
}

static bool presetMatches(const Preset& p, const HwCfg& h) {
  if (h.numCh != p.numCh || h.relayPin != p.relay || h.buttonPin != p.button) return false;
  if (p.relay >= 0 && (h.relayActiveLow != 0) != p.relayLow) return false;
  for (int c = 0; c < p.numCh; c++) if (h.ledPin[c] != p.pins[c]) return false;
  return true;
}

int hwCurrentPreset() {
  for (int i = 0; i < kNumPresets; i++) if (presetMatches(kPresets[i], g_hw)) return i;
  return -1;
}

void hwListPresets(Print& out) {
  out.print("Board presets ('preset <name>', then 'save' and 'reboot'):\r\n");
  for (int i = 0; i < kNumPresets; i++) {
    const Preset& p = kPresets[i];
    out.printf("  %-13s %s%s\r\n      LEDs", p.name, p.title, presetMatches(p, g_hw) ? "   <- current" : "");
    for (int c = 0; c < p.numCh; c++) out.printf(" %d", p.pins[c]);
    out.print(", relay ");
    if (p.relay >= 0) out.printf("%d%s", p.relay, p.relayLow ? " (active low)" : ""); else out.print("none");
    out.print(", button ");
    if (p.button >= 0) out.printf("%d", p.button); else out.print("none");
    if (p.note[0]) out.printf("\r\n      (%s)", p.note);
    out.print("\r\n");
  }
  out.print("Check your board's pin list before restarting. The light sensor setting is kept\r\n"
            "unless the board uses that pin.\r\n");
}

// ----------------------------------------------------------------

void ledsBlank() {
  uint8_t orders[MAX_CHANNELS] = {0};
  for (int c = 0; c < Leds::numChannels(); c++) memset(Leds::buffer(c), 0, Leds::count(c) * 3);
  Leds::show(0, orders);
}
