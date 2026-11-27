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
//   Function button GPIO17; GPIO13 connector = light sensor (jumpered to GPIO33)
// ============================================================================

// Channel count and GPIOs below are first-boot defaults. They can be changed at
// runtime ("channels 6", "chan 5 pin 13", or the web page), then save + restart.
#define MAX_CHANNELS     8      // ESP32 RMT has 8 transmit channels
#define DEFAULT_CHANNELS 4

// GPIO for each output channel. Channel 1 is the first entry.
// Any ESP32 output-capable GPIO works (not 6-11, which are the flash, or
// 34-39, which are input only). GPIO1 also works, but it is the serial TX
// pin; see README "GPIO1 note".
static const int8_t DEFAULT_LED_PINS[DEFAULT_CHANNELS] = { 16, 12, 4, 2 };

// Largest number of LEDs any one channel can be set to (RAM is reserved for
// this on every channel in use: 4.8 KB each).
#define MAX_LEDS_PER_CHANNEL 800

// Most separate LED runs one section can be made of.
#define MAX_RUNS_PER_SECTION 16     // largest default section (Roof) has 14 runs

// Default LED count, color order and LED type per channel (all changeable at
// runtime: "chan 1 count 175", "chan 1 order GRB", "chan 1 type ws2812b").
// Types: LED_WS2811, LED_WS2812B (also WS2813/WS2815/SK6812), LED_WS2811_400K.
// Counts are from the string routing (layout/routing): panels 1, 2, 3; output 4 unused.
// Channels added at runtime beyond these start with 0 LEDs, RGB, WS2811.
static const uint16_t   DEFAULT_CHANNEL_LEDS[DEFAULT_CHANNELS]  = { 175, 331, 291, 0 };
static const ColorOrder DEFAULT_CHANNEL_ORDER[DEFAULT_CHANNELS] = { ORDER_RGB, ORDER_RGB, ORDER_RGB, ORDER_RGB };
static const LedType    DEFAULT_CHANNEL_TYPE[DEFAULT_CHANNELS]  = { LED_WS2811, LED_WS2811, LED_WS2811, LED_WS2811 };

// Relay that switches LED power. -1 if your board has none. If the strips get
// no power, flip the active-low setting. First-boot default; changeable at
// runtime ("relay pin 18", "relay low on", or pick a board with "preset").
#define DEFAULT_RELAY_PIN        18
#define DEFAULT_RELAY_ACTIVE_LOW true

// Function button: short press = display on/off, hold 5 s = start the setup
// Wi-Fi access point. -1 = none. Changeable at runtime ("button pin 17").
#define DEFAULT_BUTTON_PIN       17

// Photoresistor (LDR) for automatic daytime off. It must be read on an ADC1 pin
// (32-39): ADC2 pins can't be read while Wi-Fi is on.
// Wiring on the GL-C-017WL-D: the LDR goes on the GPIO13 connector (signal and
// ground lug). GPIO13 is ADC2, so inside the case a jumper joins the GPIO13
// connector to GPIO33, and a 10k resistor goes from 3.3V to GPIO33. More light
// pulls the pin lower, so the reading is inverted (the firmware flips it: more
// light = higher %). If you wire the LDR to 3.3V and the resistor to GND
// instead, set the sensor to not inverted. -1 = not fitted. First-boot
// defaults; changeable at runtime ("sensor pin 34", "sensor invert off").
#define DEFAULT_SENSOR_PIN     33
#define DEFAULT_SENSOR_INVERT  true
// A pin jumpered to the sensor pin (the GPIO13 connector above). At boot it is
// set to a plain input with no pull resistor, so it doesn't skew the reading,
// unless an LED output, the relay or the button uses it. -1 = none.
#define SENSOR_JUMPER_PIN      13
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
// GitHub repository whose releases 'update' checks and installs from. Each
// release must have a led-steamboat-<version>-ota.bin asset (the CI build
// attaches one when a v* tag is pushed).
#define GITHUB_REPO      "thedayowl/led-steamboat"

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
// Generated from the frame layout and string routing in layout/routing
// (config_sections.h there): channel 1 = panel 1 (bow), 2 = panel 2 (centre,
// controller), 3 = panel 3 (stern / wheel). Adjust with the serial console or
// web page (see README), then "save"; the saved values replace these.
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
  // name                    role         grp  leds                                                    color
  { "Flag",                  ROLE_STATIC, 0, "3:270-280",                                               0x00FF40 },
  { "Flagpole",              ROLE_STATIC, 0, "3:265-269, 281-284",                                      0xFFA040 },
  { "Hull",                  ROLE_STATIC, 0, "1:19-32, 40-45, 54-57, 66-67, 2:60-61, 70-77, 105-110, 119-120, 146-149, 158-159, 3:177-185", 0xFFA040 },
  { "Life ring",             ROLE_STATIC, 0, "2:78-79, 3:170-174",                                      0xFFA040 },
  { "Lower deck columns",    ROLE_STATIC, 0, "1:37-39, 68-72, 81-84, 167-170, 2:0-3, 53-59, 121-125, 127-130, 140-145, 160-164, 171-173", 0xFF0040 },
  { "Lower deck roof",       ROLE_STATIC, 0, "1:73-80, 85-100, 171-174, 2:4-21, 48-52, 126, 131-139, 165-170, 174-183, 3:164-166", 0xFFA040 },
  { "Lower deck windows",    ROLE_STATIC, 0, "1:33-36, 46-53, 58-65, 2:62-69, 111-118, 150-157",        0xFFA040 },
  { "Rear structures",       ROLE_STATIC, 0, "3:152-155, 167-169, 175-176",                             0xFF5000 },
  { "Roof",                  ROLE_STATIC, 0, "1:104-112, 118-120, 145-154, 158-163, 2:25-28, 32-36, 40-44, 187-192, 196-203, 258-269, 278-281, 287-292, 3:156-160, 254-264", 0x00A0FF },
  { "Roof cabin",            ROLE_STATIC, 0, "2:282-286",                                               0xFF0040 },
  { "Roof dome",             ROLE_STATIC, 0, "2:270-277",                                               0xFFA040 },
  { "Smokestack",            ROLE_STATIC, 0, "2:204-210, 215, 220-222, 249-257",                        0xFFA040 },
  { "Smokestack band",       ROLE_STATIC, 0, "2:211-214, 216-219",                                      0x00A0FF },
  { "Smokestack top",        ROLE_STATIC, 0, "2:223-232, 247-248",                                      0xFFA040 },
  { "Upper deck columns",    ROLE_STATIC, 0, "1:101-103, 155-157, 164-166, 2:22-24, 29-31, 37-39, 45-47, 184-186, 193-195, 3:161-163", 0x00FF40 },
  { "Wheel axle",            ROLE_STATIC, 0, "3:219-223",                                               0xFFA040 },
  { "Wheel hub",             ROLE_STATIC, 0, "3:11-12, 21-35",                                          0xFFA040 },
  { "Wheel mount",           ROLE_STATIC, 0, "3:107-151",                                               0xFFA040 },
  { "Wheel rim",             ROLE_STATIC, 0, "3:40-69, 71-73, 75-77, 79-81, 83-85, 87-89, 91-93, 95-97, 99-103, 105-106", 0xFFA040 },
  { "Wheelhouse",            ROLE_STATIC, 0, "1:121-123, 131-136, 142-144",                             0xFF0040 },
  { "Wheelhouse door",       ROLE_STATIC, 0, "1:113-117",                                               0xFFA040 },
  { "Wheelhouse roof",       ROLE_STATIC, 0, "1:128-130, 137-141",                                      0x00A0FF },
  { "Wheelhouse top",        ROLE_STATIC, 0, "1:124-127",                                               0xFFA040 },
  { "Waterline",             ROLE_WATER,  0, "1:18-0, 2:104-80, 3:0-6, 186-197, 204-206",               0x0040FF },
  { "Smoke puff 1",          ROLE_SMOKE,  0, "2:233-246",                                               0xFFB060 },
  { "Smoke puff 2",          ROLE_SMOKE,  1, "2:303-330",                                               0xFFB060 },
  { "Smoke puff 3",          ROLE_SMOKE,  2, "2:293-302, 3:285-290",                                    0xFFB060 },
  { "Spoke 01",              ROLE_SPOKE,  0, "3:36-39, 104",                                            0xFF0030 },
  { "Spoke 02",              ROLE_SPOKE,  2, "3:249-253",                                               0xFF0030 },
  { "Spoke 03",              ROLE_SPOKE,  1, "3:98, 245-248",                                           0xFF0030 },
  { "Spoke 04",              ROLE_SPOKE,  0, "3:94, 240-244",                                           0xFF0030 },
  { "Spoke 05",              ROLE_SPOKE,  2, "3:90, 236-239",                                           0xFF0030 },
  { "Spoke 06",              ROLE_SPOKE,  1, "3:86, 232-235",                                           0xFF0030 },
  { "Spoke 07",              ROLE_SPOKE,  0, "3:82, 228-231",                                           0xFF0030 },
  { "Spoke 08",              ROLE_SPOKE,  2, "3:78, 224-227",                                           0xFF0030 },
  { "Spoke 09",              ROLE_SPOKE,  1, "3:74, 215-218",                                           0xFF0030 },
  { "Spoke 10",              ROLE_SPOKE,  0, "3:70, 211-214",                                           0xFF0030 },
  { "Spoke 11",              ROLE_SPOKE,  2, "3:207-210",                                               0xFF0030 },
  { "Spoke 12",              ROLE_SPOKE,  1, "3:198-200",                                               0xFF0030 },
  { "Spoke 13",              ROLE_SPOKE,  0, "3:201-203",                                               0xFF0030 },
  { "Spoke 14",              ROLE_SPOKE,  2, "3:13-15",                                                 0xFF0030 },
  { "Spoke 15",              ROLE_SPOKE,  1, "3:16-20",                                                 0xFF0030 },
  { "Spoke 16",              ROLE_SPOKE,  0, "3:7-10",                                                  0xFF0030 },
};
