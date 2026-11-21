// app.h - shared state and functions
#pragma once
#include <Arduino.h>
#include "config.h"

// Set at build time from the git tag by scripts/version.py (e.g. "1.2.0" or
// "1.2.0-3-gabc1234-dirty"). This fallback is only used without PlatformIO/git,
// e.g. Arduino IDE builds.
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

#define NUM_SECTIONS ((int)(sizeof(DEFAULT_SECTIONS) / sizeof(DEFAULT_SECTIONS[0])))

// One unbroken run of LEDs on one channel. first > last = wired backwards.
struct Run {
  uint8_t  ch;      // 0-based
  uint8_t  pad;
  uint16_t first;
  uint16_t last;
};

// Runtime (editable, saved) part of a section.
struct SectionCfg {
  uint32_t color;
  uint8_t  reverse;   // play the whole section in the opposite order
  uint8_t  nruns;
  uint16_t pad;
  Run      runs[MAX_RUNS_PER_SECTION];
};

struct ChannelCfg {
  uint16_t count;
  uint8_t  order;
  uint8_t  type;    // LedType (was padding, so older saved settings read as WS2811)
};

struct Settings {
  uint8_t  brightness;
  uint8_t  spokeFloor;
  uint8_t  smokeFade;
  uint8_t  wheelReverse;
  uint16_t waterSpeed;
  uint16_t smokeSpeed;
  uint16_t wheelSpeed;
  uint8_t  testBackground;   // show the boat dimly behind test patterns
  uint8_t  autoDark;         // turn off in daylight using the light sensor
  uint8_t  darkLevel;        // % light below which the display comes on
  uint8_t  dayLevel;         // % light above which the display goes off
  uint16_t sensorDelay;      // seconds
};

// Network settings (saved separately from the show; "defaults" doesn't touch them)
struct NetCfg {
  char staSsid[33];    // home network ("" = access point only)
  char staPass[64];
  char apSsid[33];     // setup access point
  char apPass[64];     // WPA2, 8-63 characters
  char adminPass[64];  // web page (user "admin") and OTA uploads
};

enum TestMode : uint8_t {
  TEST_OFF = 0,  // normal show
  TEST_PIXEL,    // one LED (cursor)
  TEST_RANGE,    // LEDs a..b on one channel
  TEST_SECTION,  // one section: dot chases through it in animation order
  TEST_ENDS,     // every run of every section: start green, end red
  TEST_RULER,    // one channel: every 10th red, every 5th blue
  TEST_FILL,     // one channel filled with a color
  TEST_RGB,      // one channel R,G,B,R,G,B... to check color order
  TEST_WALK,     // cursor steps automatically
  TEST_BLANK     // everything off
};

struct TestState {
  TestMode mode;
  uint8_t  ch;        // 0-based channel (cursor)
  uint16_t a;         // cursor LED / range start
  uint16_t b;         // range end
  int16_t  section;   // for TEST_SECTION
  uint32_t color;     // for TEST_FILL
  uint16_t walkMs;
  uint32_t lastStep;
};

extern SectionCfg g_sect[];
extern ChannelCfg g_chan[NUM_CHANNELS];
extern Settings   g_set;
extern TestState  g_test;
extern bool       g_dirty;   // unsaved changes
extern bool       g_on;      // display on (button / "on"/"off")
extern NetCfg     g_net;

// display.cpp
const char* sectionName(int i);
Role        sectionRole(int i);
uint8_t     sectionGroup(int i);
uint16_t    runLength(const Run& r);
uint16_t    sectionCount(int i);                                   // total LEDs in all runs
bool        sectionOnChannel(int i, uint8_t ch);
// Parse "1:0-89, 1:200-170, 2:5" into runs. Returns false and sets err on error.
bool        parseRuns(const String& text, Run* runs, uint8_t& n, uint8_t maxRuns, String& err);
String      formatRuns(int i);
void        displayRender(uint32_t now);
void        loadConfig();
bool        saveConfig();
void        loadDefaults();
int         findSection(const String& id);                         // -1 none, -2 ambiguous
// Section containing a LED (last drawn wins); offset = position in animation order
int         sectionAt(uint8_t ch, uint16_t led, uint16_t* offset);
int         numGroups(Role r);
int         validate(Print& out, bool verbose);                    // returns error count
bool        testUsesChannel(uint8_t ch);
const char* orderName(uint8_t o);
int         parseOrder(const String& s);
const char* ledTypeName(uint8_t t);
int         parseLedType(const String& s);                         // -1 unknown
uint8_t     ledTypeOrder(uint8_t t);                               // usual ColorOrder for the type
const char* roleName(Role r);

// console.cpp
void consoleBegin();
void consolePoll();
void consoleTick(uint32_t now);
bool runCommand(const String& line, Print& out);   // false if the command failed
void writeBackup(Print& out, bool secrets);         // configuration as console commands
bool consoleActive();
void touchActivity();
void updatePinRouting();

// web.cpp
void webBegin();
void webLoop();

// ghupdate.cpp - firmware updates from GitHub releases
enum GhState : uint8_t { GH_IDLE = 0, GH_CHECKING, GH_DOWNLOADING, GH_DONE, GH_FAILED };
bool        ghCheck(Print& out);                       // ask GitHub for the latest release
bool        ghInstall(const String& tag, Print& out);  // "" = latest if newer; starts a background download
void        ghStatus(Print& out);
bool        ghNewer(const char* tag);                  // release newer than the running firmware?
GhState     ghState();
bool        ghBusy();                                  // downloading or about to restart: keep LEDs dark
int         ghPercent();                               // -1 unknown
const char* ghLatest();                                // tag from the last check, "" if none
const char* ghMessage();

// main.cpp
String netInfo();
void   startSetupAP();
bool   sensorPresent();
int    lightLevel();      // 0-100 %, smoothed
int    lightRaw();        // 0-4095, smoothed
bool   isDaylight();      // sensor says it is day (after hysteresis and delay)
bool   displayActive();   // show should be lit: on, and not switched off by daylight
void   sensorStatus(Print& out);
