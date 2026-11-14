// config.h - everything you are likely to change lives here.
//
// Section LED assignments and colors can also be changed at runtime from the
// serial console or the web page and saved to flash, so you only need to edit
// the table below if you want different compiled-in defaults or want to add
// or remove sections.
#pragma once
#include "types.h"

// ============================================================================
// Board: GLEDOPTO GL-C-017WL-D (ESP32, 4 outputs)
//   Terminal order on the board: GPIO16  GPIO12  GPIO4  GPIO2
//   Relay GPIO18 (active LOW, same as Gledopto's "Relay 18 + Invert" WLED setup)
//   Function button GPIO17, spare GPIO13
// ============================================================================

#define NUM_CHANNELS 4

// GPIO for each output channel. Channel 1 is the first entry.
// Change these to match your board. Any ESP32 output-capable GPIO works.
// GPIO1 also works, but it is the serial TX pin; see README "GPIO1 note".
static const uint8_t LED_PINS[NUM_CHANNELS] = { 16, 12, 4, 2 };

// Largest number of LEDs any one channel can be set to (RAM is reserved for this).
#define MAX_LEDS_PER_CHANNEL 800

// Most separate LED runs one section can be made of.
#define MAX_RUNS_PER_SECTION 12

// Default LED count and color order per channel (both changeable at runtime).
static const uint16_t   DEFAULT_CHANNEL_LEDS[NUM_CHANNELS]  = { 300, 300, 300, 300 };
static const ColorOrder DEFAULT_CHANNEL_ORDER[NUM_CHANNELS] = { ORDER_RGB, ORDER_RGB, ORDER_RGB, ORDER_RGB };

// Relay that switches LED power. Set RELAY_PIN to -1 if your board has none.
// If the strips get no power, flip RELAY_ACTIVE_LOW.
#define RELAY_PIN        18
#define RELAY_ACTIVE_LOW true

// Function button: short press = display on/off, hold 5 s = start the setup
// Wi-Fi access point. Set to -1 to disable.
#define BUTTON_PIN       17

// Photoresistor (LDR) for automatic daytime off. Use an ADC1 pin (32-39) so it
// works with Wi-Fi on. Wiring: 3.3V -> LDR -> GPIO33 -> 10k resistor -> GND
// (more light = higher reading). If you wire the LDR to GND and the resistor to
// 3.3V instead, set LIGHT_SENSOR_INVERT true. Set the pin to -1 if not fitted.
#define LIGHT_SENSOR_PIN     33
#define LIGHT_SENSOR_INVERT  false
// Defaults (changeable at runtime with the "sensor" command or web page):
#define DEFAULT_AUTO_DARK    false   // turn on with "sensor on" after checking the readings
#define DEFAULT_DARK_LEVEL   20      // % light: display turns ON when it gets darker than this
#define DEFAULT_DAY_LEVEL    35      // % light: display turns OFF when it gets brighter than this
#define DEFAULT_SENSOR_DELAY 60      // seconds the level must stay past a threshold before switching

// Serial console
#define SERIAL_BAUD      115200
#define SERIAL_TX_PIN    1      // ESP32 UART0 pins (wired to the USB-serial chip)
#define SERIAL_RX_PIN    3

// ============================================================================
// Network
// These are first-boot defaults. They can be changed at runtime from the
// console or web page ("wifi", "ap", "admin" commands), then "save" + reboot.
// ============================================================================
// Home network. Leave WIFI_SSID empty to run as an access point only.
#define WIFI_SSID        ""
#define WIFI_PASSWORD    ""
#define HOSTNAME         "steamboat"          // http://steamboat.local
// Setup access point, started if the home network can't be joined within
// 20 s (or immediately when no network is set). Web page at http://192.168.4.1
#define AP_SSID          "Steamboat-Setup"
#define AP_PASSWORD      "steamboat"          // WPA2, 8-63 characters
// Password for the web page (user name "admin") and for OTA uploads.
#define ADMIN_PASSWORD   "steamboat"
#define WIFI_CONNECT_TIMEOUT_MS 20000

// ============================================================================
// Show defaults (all changeable at runtime and saved with "save")
// ============================================================================
#define DEFAULT_BRIGHTNESS   160   // 0-255 master brightness
#define DEFAULT_WATER_SPEED  100   // percent
#define DEFAULT_SMOKE_SPEED  100   // percent
#define DEFAULT_WHEEL_SPEED  100   // percent
#define DEFAULT_SPOKE_FLOOR  20    // 0-255 brightness of the spokes that are "off"
#define DEFAULT_SMOKE_FADE   70    // 0-255 how much dimmer each puff is than the one before

#define FRAME_MS             20    // 50 frames per second

// Water: colors from darkest to lightest. The waves blend through these.
static const uint32_t WATER_COLORS[4] = { 0x000A40, 0x0030D0, 0x0080FF, 0x70C8FF };

// Smoke timing at 100% speed (milliseconds)
#define SMOKE_STAGGER_MS  700      // delay between one puff starting and the next
#define SMOKE_RISE_MS     350      // fade in
#define SMOKE_HOLD_MS     350      // full brightness
#define SMOKE_FALL_MS     900      // fade out
#define SMOKE_PAUSE_MS    500      // dark gap after the last puff, before the next burst

// Paddle wheel timing at 100% speed
#define WHEEL_STEP_MS     350      // time for the light to move from one spoke set to the next
#define WHEEL_FADE        0.5f     // fraction of each step spent crossfading (0 = hard switch, 1 = always fading)

// Test mode / console: return to the normal show after this long with no input
#define TEST_TIMEOUT_MS   (15UL * 60UL * 1000UL)

// ============================================================================
// Default section table
//
// The LED numbers below are placeholders. Map your real strings with the serial
// console or web page (see README), then "save"; the saved values replace these.
//
// LED runs ("leds" column):
//   "ch:first-last"        one run, e.g. "1:0-89"   (LED numbers start at 0)
//   "ch:led"               a single LED, e.g. "2:17"
//   several runs, separated by commas, in the order the animation should
//   travel: "1:90-120, 1:200-170, 2:5-9"
//   first > last means the run is wired backwards: "1:200-170" goes 200,199...170
//   the channel may be left off to reuse the previous one: "1:0-10, 40-50, 90-80"
//   "" = no LEDs yet
//
// The order matters for the waterline: water flows from the first LED of the
// first run toward the last LED of the last run. List backwards-wired pieces
// with first > last so the flow stays continuous. ("rev" in the console flips
// a whole section.) For static/smoke/spoke sections order doesn't matter.
//
//  * Smoke puffs: group 0 is the puff nearest the stack, then 1, 2...
//  * Spokes: groups 0,1,2... in the order the light should travel around the
//    wheel. Several sections can share a group.
//  * Later rows draw on top of earlier rows if they overlap.
//  * If you add, remove, or rename rows, saved section settings are discarded
//    at the next boot and these defaults are used.
// ============================================================================
#define WARM_WHITE 0xFFA040
#define SMOKE_GRAY 0xFFB060

static const SectionDef DEFAULT_SECTIONS[] = {
  // name                   role         grp  leds               color
  // ---- Channel 1 (GPIO16): hull and lower deck ----
  { "Hull",                 ROLE_STATIC,  0, "1:0-89",           WARM_WHITE },
  { "Waterline",            ROLE_WATER,   0, "1:90-159",         0x0040FF   },
  { "Lower deck windows",   ROLE_STATIC,  0, "1:160-249",        WARM_WHITE },
  { "Lower deck columns",   ROLE_STATIC,  0, "1:250-289",        0xFF0040   },
  // ---- Channel 2 (GPIO12): upper deck and wheelhouse ----
  { "Upper deck windows",   ROLE_STATIC,  0, "2:0-59",           0x00FF60   },
  { "Roof",                 ROLE_STATIC,  0, "2:60-129",         0x00A0FF   },
  { "Rear structures",      ROLE_STATIC,  0, "2:130-169",        0xFF5000   },
  { "Wheelhouse",           ROLE_STATIC,  0, "2:170-194",        0xFF0040   },
  { "Wheelhouse roof",      ROLE_STATIC,  0, "2:195-206",        0x00A0FF   },
  { "Wheelhouse door",      ROLE_STATIC,  0, "2:207-212",        WARM_WHITE },
  { "Flagpole",             ROLE_STATIC,  0, "2:213-220",        WARM_WHITE },
  { "Flag",                 ROLE_STATIC,  0, "2:221-232",        0x00FF40   },
  // ---- Channel 3 (GPIO4): smokestack and smoke ----
  { "Smokestack",           ROLE_STATIC,  0, "3:0-29",           WARM_WHITE },
  { "Smokestack band",      ROLE_STATIC,  0, "3:30-39",          0x00A0FF   },
  { "Smokestack top",       ROLE_STATIC,  0, "3:40-59",          WARM_WHITE },
  { "Smoke puff 1",         ROLE_SMOKE,   0, "3:60-73",          SMOKE_GRAY },
  { "Smoke puff 2",         ROLE_SMOKE,   1, "3:74-87",          SMOKE_GRAY },
  { "Smoke puff 3",         ROLE_SMOKE,   2, "3:88-101",         SMOKE_GRAY },
  // ---- Channel 4 (GPIO2): paddle wheel ----
  { "Wheel mount",          ROLE_STATIC,  0, "4:0-29",           WARM_WHITE },
  { "Wheel hub",            ROLE_STATIC,  0, "4:30-37",          WARM_WHITE },
  { "Wheel rim",            ROLE_STATIC,  0, "4:38-117",         WARM_WHITE },
  { "Spokes A",             ROLE_SPOKE,   0, "4:118-137",        0xFF0030   },
  { "Spokes B",             ROLE_SPOKE,   1, "4:138-157",        0xFF0030   },
  { "Spokes C",             ROLE_SPOKE,   2, "4:158-177",        0xFF0030   },
};
