// console.cpp - serial test console (also used by the web page's command box)
//
// Press Enter in a serial terminal (115200 baud) to enter the console. The
// show keeps running until you start a test pattern. "run" (or 15 minutes of
// no input) goes back to the normal show.
#include <stdarg.h>
#include "app.h"
#include "led_driver.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"

static int      s_txCh = -1;       // LED channel whose GPIO is the serial TX pin (-1 none)
static bool     s_rxOk = true;     // serial RX pin is free for input
static bool     s_active = false;  // console session open
static uint32_t s_lastActivity = 0;
static String   s_line;
static uint8_t  s_esc = 0;
static bool     s_lastCR = false;
static bool     s_failed = false;  // set by fail() while a command runs

// Output to the serial port, dropped while the TX pin is driving LEDs.
static bool txMuted() { return s_txCh >= 0 && Leds::attached(s_txCh); }
class ConsoleOut : public Print {
 public:
  size_t write(uint8_t c) override { return txMuted() ? 1 : Serial.write(c); }
  size_t write(const uint8_t* b, size_t n) override { return txMuted() ? n : Serial.write(b, n); }
};
static ConsoleOut s_out;

bool consoleActive() { return s_active; }
void touchActivity() { s_lastActivity = millis(); }

// Print an error and mark the command as failed (used by restore)
static void fail(Print& out, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void fail(Print& out, const char* fmt, ...) {
  s_failed = true;
  char b[256];
  va_list a;
  va_start(a, fmt);
  vsnprintf(b, sizeof(b), fmt, a);
  va_end(a);
  out.print(b);
}

static void printLines(Print& out, const char* text) {
  for (const char* p = text; *p; p++) {
    if (*p == '\n') out.print("\r\n");
    else out.write((uint8_t)*p);
  }
}

static const char* kHelp = R"(
Steamboat console. LED numbers start at 0, channels and sections at 1.
Sections can be given by number (see 'list') or name ("roof", "puff2", "spokesa").

 Finding LEDs
  p <ch> <led>         light one LED (the cursor)      p <led>  same channel
  + / -  or  -> / <-   move cursor 1 LED (no Enter needed in a terminal)
  ] / [  or  up / down move cursor 10 LEDs
  walk <ch> [ms]       step through LEDs automatically (any key stops)
  r <ch> <a> <b>       light LEDs a..b
  ruler <ch>           LED 0 green, every 5th blue, 10th red, 100th magenta
  fill <ch> [RRGGBB]   light a whole channel (checks LED count)
  rgb <ch>             red,green,blue,red... (checks color order)
  s <section>          show a section: a white dot travels through it in
                       animation order; each run starts green, ends red
  ends                 every run of every section: start green, end red
  blank                all LEDs off
  bg on|off            show the boat dimly behind test patterns
  run                  back to the normal show

 Editing sections (changes are live; 'save' keeps them)
  A section is a list of runs, in animation order:  1:0-89, 1:300-280, 2:5
  (first > last = wired backwards; the water flows along the list in order)
  list                 section table          chans   channel table
  check                look for overlaps, overruns, unassigned LEDs
  ms <section>         start the section over: first run begins at the cursor
  ma <section>         add another run beginning at the cursor
  me <section>         end the last run at the cursor (before its start = backwards)
  set <section> <runs> replace all runs, e.g.  set waterline 1:90-120, 1:200-170
  add <section> <runs> append runs
  clear <section>      remove all runs
  color <section> <RRGGBB>
  rev <section> [on|off]  play the section in the opposite order (water direction)
  chan <ch> count <n>  LEDs on a channel     chan <ch> order <RGB|GRB|BRG...>
  save                 write to flash        reload  discard unsaved changes
  defaults             compiled-in show/section defaults (network unchanged)

 Show settings
  bright <0-255>       speed <water|smoke|wheel> <percent>
  floor <0-255>        brightness of unlit spokes
  fade <0-255>         how much dimmer each farther smoke puff is
  wheeldir [normal|reversed]  paddle wheel direction
  on / off             display (and LED power relay) on or off

 Daylight auto-off (photoresistor)
  sensor               current light level and state
  sensor on|off        enable/disable turning off in daylight
  sensor dark <0-100>  % light below which the display turns on
  sensor day <0-100>   % light above which the display turns off
  sensor delay <s>     how long the light must stay past a threshold

 Network (take effect after 'save' and 'reboot')
  wifi                 show network settings
  wifi ssid <name>     home network            wifi pass <password|none>
  wifi off             no home network (access point only)
  ap ssid <name>       setup access point name ap pass <8-63 chars>  (WPA2)
  admin pass <pw>      web page (user admin) and OTA upload password

 Backup
  backup               print the configuration as commands (paste back to restore)
  backup secrets       same, including Wi-Fi/AP/admin passwords

  info   reboot   help
)";

static void reportCursor(Print& out) {
  uint16_t off = 0;
  int s = sectionAt(g_test.ch, g_test.a, &off);
  out.printf("ch%d LED %u", g_test.ch + 1, g_test.a);
  if (s >= 0) out.printf("   -> #%d %s (%u of %u)", s + 1, sectionName(s), off + 1, sectionCount(s));
  else out.print("   -> not in any section");
  out.print("\r\n");
}

static void routeTxToUart() {
  esp_rom_gpio_pad_select_gpio(SERIAL_TX_PIN);
  gpio_set_direction((gpio_num_t)SERIAL_TX_PIN, GPIO_MODE_OUTPUT);
  esp_rom_gpio_connect_out_signal(SERIAL_TX_PIN, U0TXD_OUT_IDX, false, false);
}

// Decide who owns a shared TX pin: the LEDs (normal show, or testing that
// channel) or the serial port (console open, testing something else).
void updatePinRouting() {
  if (s_txCh < 0) return;
  bool wantLeds = !s_active || testUsesChannel((uint8_t)s_txCh);
  if (wantLeds && !Leds::attached(s_txCh)) {
    if (s_active)
      s_out.printf("\r\n[Channel %d is on GPIO%d, which is also the serial TX pin. Console output is "
                   "paused while that channel is lit; typing still works. Type 'blank', 'run', or test "
                   "another channel to get output back.]\r\n", s_txCh + 1, SERIAL_TX_PIN);
    Serial.flush();
    Leds::attachPin(s_txCh);
  } else if (!wantLeds && Leds::attached(s_txCh)) {
    Leds::releasePin(s_txCh);
    routeTxToUart();
    delay(2);
  }
}

static void printBanner() {
  s_out.printf("\r\n=== Steamboat lights %s - console ===\r\n", FW_VERSION);
  s_out.printf("%s\r\n", netInfo().c_str());
  if (s_txCh >= 0)
    s_out.printf("Note: channel %d shares GPIO%d with serial TX; its string shows junk while the console "
                 "is talking. Use the web page to map that channel.\r\n", s_txCh + 1, SERIAL_TX_PIN);
  s_out.print("Type 'help' for commands, 'run' to return to the show.\r\n");
}

static void prompt() { if (s_active) s_out.print(g_dirty ? "steamboat*> " : "steamboat> "); }

static void activate() {
  s_active = true;
  touchActivity();
  updatePinRouting();
  printBanner();
}

static void deactivate(Print& out) {
  g_test.mode = TEST_OFF;
  out.print("Back to the show. Press Enter for the console.\r\n");
  if (g_dirty) out.print("(You have unsaved changes; they stay until reboot. 'save' keeps them.)\r\n");
  Serial.flush();
  s_active = false;
  updatePinRouting();
}

void consoleBegin() {
  for (int c = 0; c < NUM_CHANNELS; c++) {
    if (LED_PINS[c] == SERIAL_TX_PIN) s_txCh = c;
    if (LED_PINS[c] == SERIAL_RX_PIN) s_rxOk = false;
  }
  g_test.mode = TEST_OFF;
  g_test.ch = 0; g_test.a = 0; g_test.b = 0; g_test.section = -1;
  g_test.color = 0xFFFFFF; g_test.walkMs = 400; g_test.lastStep = 0;
  if (!s_rxOk) {
    s_out.printf("GPIO%d (serial RX) is used for LEDs, so the serial console is disabled. "
                 "Use the web page.\r\n", SERIAL_RX_PIN);
  } else {
    s_out.print("Press Enter for the test console.\r\n");
  }
  Serial.flush();
}

// ---------------------------------------------------------------- parsing helpers

static int tokenize(const String& line, String* tok, int maxTok) {
  int n = 0; size_t i = 0;
  while (i < line.length() && n < maxTok) {
    while (i < line.length() && line[i] == ' ') i++;
    if (i >= line.length()) break;
    size_t j = i;
    while (j < line.length() && line[j] != ' ') j++;
    tok[n++] = line.substring(i, j);
    i = j;
  }
  return n;
}

// Text after the first k space-separated words
static String restOfLine(const String& line, int k) {
  size_t i = 0;
  for (int w = 0; w < k; w++) {
    while (i < line.length() && line[i] == ' ') i++;
    while (i < line.length() && line[i] != ' ') i++;
  }
  String r = line.substring(i);
  r.trim();
  return r;
}

static bool parseNum(const String& s, long& v) {
  if (s.length() == 0) return false;
  size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
  if (i >= s.length()) return false;
  for (size_t k = i; k < s.length(); k++) if (!isdigit((unsigned char)s[k])) return false;
  v = s.toInt();
  return true;
}

static bool parseColor(String s, uint32_t& c) {
  if (s.startsWith("#")) s = s.substring(1);
  if (s.startsWith("0x") || s.startsWith("0X")) s = s.substring(2);
  if (s.length() != 6) return false;
  for (size_t k = 0; k < 6; k++) if (!isxdigit((unsigned char)s[k])) return false;
  c = strtoul(s.c_str(), NULL, 16);
  return true;
}

// on/off/1/0/yes/no; returns -1 if not recognised
static int parseOnOff(String s) {
  s.toLowerCase();
  if (s == "on" || s == "1" || s == "yes" || s == "true") return 1;
  if (s == "off" || s == "0" || s == "no" || s == "false") return 0;
  return -1;
}

static bool argChannel(const String& s, uint8_t& ch, Print& out) {
  long v;
  if (!parseNum(s, v) || v < 1 || v > NUM_CHANNELS) { fail(out, "Channel must be 1-%d\r\n", NUM_CHANNELS); return false; }
  ch = (uint8_t)(v - 1);
  return true;
}

static bool argSection(const String& s, int& idx, Print& out) {
  idx = findSection(s);
  if (idx == -2) { fail(out, "'%s' matches more than one section; be more specific or use the number.\r\n", s.c_str()); return false; }
  if (idx < 0)   { fail(out, "No section '%s'. Type 'list'.\r\n", s.c_str()); return false; }
  return true;
}

static void printSection(Print& out, int i) {
  const SectionCfg& s = g_sect[i];
  char anim[16];
  if (sectionRole(i) == ROLE_SMOKE || sectionRole(i) == ROLE_SPOKE) snprintf(anim, sizeof(anim), "%s %d", roleName(sectionRole(i)), sectionGroup(i) + 1);
  else snprintf(anim, sizeof(anim), "%s", roleName(sectionRole(i)));
  out.printf("%3d  %-20s %-8s %06lX %5u  %s%s\r\n", i + 1, sectionName(i), anim, (unsigned long)s.color,
             sectionCount(i), s.nruns ? formatRuns(i).c_str() : "(none)", s.reverse ? "   [reversed]" : "");
}

static void listHeader(Print& out) { out.print("  #  Section              Anim     Color  LEDs   Runs (ch:first-last)\r\n"); }

static void listSections(Print& out) {
  listHeader(out);
  for (int i = 0; i < NUM_SECTIONS; i++) printSection(out, i);
}

static void listChannels(Print& out) {
  out.print(" Ch  GPIO  LEDs  Order\r\n");
  for (int c = 0; c < NUM_CHANNELS; c++)
    out.printf(" %2d  %4d  %4u  %s%s\r\n", c + 1, LED_PINS[c], g_chan[c].count, orderName(g_chan[c].order),
               Leds::ok(c) ? "" : "  (output failed to start)");
}

static void showNetwork(Print& out) {
  out.printf("Home network: %s%s\r\n", g_net.staSsid[0] ? g_net.staSsid : "(none - access point only)",
             g_net.staSsid[0] ? (g_net.staPass[0] ? "  (password set)" : "  (open network)") : "");
  out.printf("Setup AP:     %s  (WPA2, password %d characters)\r\n", g_net.apSsid, (int)strlen(g_net.apPass));
  out.printf("Admin password: %d characters (web user name 'admin')\r\n", (int)strlen(g_net.adminPass));
  out.printf("Now: %s\r\n", netInfo().c_str());
}

static void setMode(TestMode m) {
  g_test.mode = m;
  if (m == TEST_WALK) g_test.lastStep = millis();
  updatePinRouting();   // before printing, so a released TX pin can talk again
}

static void stepCursor(int d, Print& out) {
  uint16_t n = g_chan[g_test.ch].count;
  if (n == 0) { fail(out, "Channel has 0 LEDs\r\n"); return; }
  long a = (long)g_test.a + d;
  if (a < 0) a = 0;
  if (a >= n) a = n - 1;
  g_test.a = (uint16_t)a;
  setMode(TEST_PIXEL);
  reportCursor(out);
}

static void changed() { g_dirty = true; }

static void warnRuns(Print& out, int i) {
  const SectionCfg& s = g_sect[i];
  for (uint8_t r = 0; r < s.nruns; r++) {
    const Run& run = s.runs[r];
    uint16_t hi = run.first > run.last ? run.first : run.last;
    if (hi >= g_chan[run.ch].count)
      out.printf("Warning: run %d goes past the end of ch%d (%u LEDs)\r\n", r + 1, run.ch + 1, g_chan[run.ch].count);
  }
}

static String keyName(int i) {   // section name as one word, for backups
  String o;
  for (const char* p = sectionName(i); *p; p++) if (isalnum((unsigned char)*p)) o += (char)tolower((unsigned char)*p);
  return o;
}

// ---------------------------------------------------------------- backup

void writeBackup(Print& out, bool secrets) {
  out.printf("# Steamboat lights configuration backup (firmware %s)\r\n", FW_VERSION);
  out.print("# Restore: web page > Backup & restore, or paste into the serial console.\r\n");
  out.print("# Lines starting with # are ignored. Sections are matched by name.\r\n");
  out.print("\r\n# Channels\r\n");
  for (int c = 0; c < NUM_CHANNELS; c++) {
    out.printf("chan %d count %u\r\n", c + 1, g_chan[c].count);
    out.printf("chan %d order %s\r\n", c + 1, orderName(g_chan[c].order));
  }
  out.print("\r\n# Sections\r\n");
  for (int i = 0; i < NUM_SECTIONS; i++) {
    String k = keyName(i);
    if (g_sect[i].nruns) out.printf("set %s %s\r\n", k.c_str(), formatRuns(i).c_str());
    else out.printf("clear %s\r\n", k.c_str());
    out.printf("color %s %06lX\r\n", k.c_str(), (unsigned long)g_sect[i].color);
    out.printf("rev %s %s\r\n", k.c_str(), g_sect[i].reverse ? "on" : "off");
  }
  out.print("\r\n# Show\r\n");
  out.printf("bright %u\r\n", g_set.brightness);
  out.printf("speed water %u\r\nspeed smoke %u\r\nspeed wheel %u\r\n", g_set.waterSpeed, g_set.smokeSpeed, g_set.wheelSpeed);
  out.printf("floor %u\r\nfade %u\r\n", g_set.spokeFloor, g_set.smokeFade);
  out.printf("wheeldir %s\r\n", g_set.wheelReverse ? "reversed" : "normal");
  out.printf("bg %s\r\n", g_set.testBackground ? "on" : "off");
  if (sensorPresent()) {
    out.print("\r\n# Light sensor\r\n");
    out.printf("sensor dark %u\r\nsensor day %u\r\nsensor delay %u\r\nsensor %s\r\n",
               g_set.darkLevel, g_set.dayLevel, g_set.sensorDelay, g_set.autoDark ? "on" : "off");
  }
  out.print("\r\n# Network (applies after reboot)\r\n");
  if (g_net.staSsid[0]) out.printf("wifi ssid %s\r\n", g_net.staSsid);
  else out.print("wifi off\r\n");
  out.printf("ap ssid %s\r\n", g_net.apSsid);
  if (secrets) {
    out.printf("wifi pass %s\r\n", g_net.staPass[0] ? g_net.staPass : "none");
    out.printf("ap pass %s\r\n", g_net.apPass);
    out.printf("admin pass %s\r\n", g_net.adminPass);
  } else {
    out.print("# (passwords not included; use 'backup secrets' to include them)\r\n");
  }
}

// ---------------------------------------------------------------- commands

static void doCommand(const String& line, Print& out);

bool runCommand(const String& lineIn, Print& out) {
  touchActivity();
  String line = lineIn;
  line.trim();
  if (line.length() == 0 || line[0] == '#') return true;   // blank / comment
  s_failed = false;
  doCommand(line, out);
  return !s_failed;
}

static void doCommand(const String& line, Print& out) {
  String t[8];
  int n = tokenize(line, t, 8);
  if (n == 0) return;
  String cmd = t[0];
  cmd.toLowerCase();
  long v;

  // cursor movement: + - [ ] +N -N
  if (cmd == "+" || cmd == "=") { stepCursor(1, out); return; }
  if (cmd == "-" || cmd == "_") { stepCursor(-1, out); return; }
  if (cmd == "]") { stepCursor(10, out); return; }
  if (cmd == "[") { stepCursor(-10, out); return; }
  if ((cmd[0] == '+' || cmd[0] == '-') && parseNum(cmd, v)) { stepCursor((int)v, out); return; }

  if (cmd == "help" || cmd == "?" || cmd == "h") { printLines(out, kHelp); return; }

  if (cmd == "run" || cmd == "exit" || cmd == "q" || cmd == "quit") { deactivate(out); return; }

  if (cmd == "p" || cmd == "pixel") {
    uint8_t ch = g_test.ch; String num;
    if (n >= 3) { if (!argChannel(t[1], ch, out)) return; num = t[2]; }
    else if (n == 2) num = t[1];
    else { setMode(TEST_PIXEL); reportCursor(out); return; }
    if (!parseNum(num, v) || v < 0 || v >= g_chan[ch].count) { fail(out, "LED must be 0-%d\r\n", g_chan[ch].count - 1); return; }
    g_test.ch = ch; g_test.a = (uint16_t)v;
    setMode(TEST_PIXEL);
    reportCursor(out);
    return;
  }

  if (cmd == "walk") {
    uint8_t ch = g_test.ch;
    if (n >= 2 && !argChannel(t[1], ch, out)) return;
    if (n >= 3 && parseNum(t[2], v) && v >= 50) g_test.walkMs = (uint16_t)(v > 10000 ? 10000 : v);
    if (ch != g_test.ch) g_test.a = 0;
    g_test.ch = ch;
    setMode(TEST_WALK);
    out.printf("Walking ch%d every %u ms. Any key stops.\r\n", ch + 1, g_test.walkMs);
    return;
  }

  if (cmd == "r" || cmd == "range") {
    uint8_t ch; long a, b;
    if (n < 4) { fail(out, "Usage: r <ch> <first> <last>\r\n"); return; }
    if (!argChannel(t[1], ch, out)) return;
    if (!parseNum(t[2], a) || !parseNum(t[3], b)) { fail(out, "Usage: r <ch> <first> <last>\r\n"); return; }
    if (a > b) { long x = a; a = b; b = x; }
    if (a < 0 || b >= g_chan[ch].count) { fail(out, "LEDs must be 0-%d\r\n", g_chan[ch].count - 1); return; }
    g_test.ch = ch; g_test.a = (uint16_t)a; g_test.b = (uint16_t)b;
    setMode(TEST_RANGE);
    out.printf("ch%d LEDs %ld-%ld (%ld LEDs)\r\n", ch + 1, a, b, b - a + 1);
    return;
  }

  if (cmd == "ruler" || cmd == "fill" || cmd == "rgb") {
    uint8_t ch = g_test.ch;
    if (n >= 2 && !argChannel(t[1], ch, out)) return;
    g_test.ch = ch;
    if (g_test.a >= g_chan[ch].count) g_test.a = 0;
    if (cmd == "ruler") {
      setMode(TEST_RULER);
      out.printf("Ruler on ch%d (%u LEDs): LED 0 green, every 5th blue, 10th red, 100th magenta.\r\n", ch + 1, g_chan[ch].count);
    } else if (cmd == "fill") {
      uint32_t c = 0xFFFFFF;
      if (n >= 3 && !parseColor(t[2], c)) { fail(out, "Color is RRGGBB hex\r\n"); return; }
      g_test.color = c;
      setMode(TEST_FILL);
      out.printf("ch%d all %u LEDs %06lX. If LEDs past the end light up, raise 'chan %d count'.\r\n",
                 ch + 1, g_chan[ch].count, (unsigned long)c, ch + 1);
    } else {
      setMode(TEST_RGB);
      out.printf("ch%d: LED 0 should be RED, 1 GREEN, 2 BLUE. Current order %s; if wrong try "
                 "'chan %d order GRB' (or BRG, RBG...).\r\n", ch + 1, orderName(g_chan[ch].order), ch + 1);
    }
    return;
  }

  if (cmd == "s" || cmd == "sec" || cmd == "section") {
    int i;
    if (n < 2) { fail(out, "Usage: s <section>\r\n"); return; }
    if (!argSection(t[1], i, out)) return;
    const SectionCfg& s = g_sect[i];
    g_test.section = (int16_t)i;
    if (s.nruns) {   // cursor to the first LED in animation order
      const Run& r = s.reverse ? s.runs[s.nruns - 1] : s.runs[0];
      g_test.ch = r.ch;
      g_test.a = s.reverse ? r.last : r.first;
    }
    setMode(TEST_SECTION);
    listHeader(out);
    printSection(out, i);
    if (s.nruns) out.print("White dot travels in animation order; each run starts green and ends red. Cursor is on its first LED.\r\n");
    return;
  }

  if (cmd == "ends") { setMode(TEST_ENDS); out.print("Every run of every section: start green, end red.\r\n"); return; }
  if (cmd == "blank") { setMode(TEST_BLANK); out.print("All LEDs off.\r\n"); return; }

  if (cmd == "bg") {
    if (n >= 2) {
      int o = parseOnOff(t[1]);
      if (o < 0) { fail(out, "Usage: bg on|off\r\n"); return; }
      g_set.testBackground = (uint8_t)o;
    } else g_set.testBackground = !g_set.testBackground;
    changed();
    out.printf("Test background %s\r\n", g_set.testBackground ? "on" : "off");
    return;
  }

  if (cmd == "list" || cmd == "ls") { listSections(out); return; }
  if (cmd == "chans" || cmd == "channels") { listChannels(out); return; }
  if (cmd == "check") { validate(out, true); return; }

  if (cmd == "ms" || cmd == "ma" || cmd == "me") {
    int i;
    if (n < 2) { fail(out, "Usage: %s <section>\r\n", cmd.c_str()); return; }
    if (!argSection(t[1], i, out)) return;
    SectionCfg& s = g_sect[i];
    Run here; here.ch = g_test.ch; here.pad = 0; here.first = g_test.a; here.last = g_test.a;
    if (cmd == "ms") {
      s.nruns = 1;
      s.runs[0] = here;
      out.printf("%s starts at ch%d LED %u. Move to the end of this run and type 'me %s'.\r\n",
                 sectionName(i), here.ch + 1, here.first, t[1].c_str());
    } else if (cmd == "ma") {
      if (s.nruns >= MAX_RUNS_PER_SECTION) { fail(out, "%s already has %d runs (MAX_RUNS_PER_SECTION).\r\n", sectionName(i), MAX_RUNS_PER_SECTION); return; }
      s.runs[s.nruns++] = here;
      out.printf("Run %d of %s starts at ch%d LED %u. Move to its end and type 'me %s'.\r\n",
                 s.nruns, sectionName(i), here.ch + 1, here.first, t[1].c_str());
    } else {
      if (s.nruns == 0) { fail(out, "%s has no runs yet; use 'ms %s' first.\r\n", sectionName(i), t[1].c_str()); return; }
      Run& last = s.runs[s.nruns - 1];
      if (last.ch != g_test.ch) {
        fail(out, "The cursor is on ch%d but %s's last run is on ch%d. A run can't cross channels; "
                  "use 'ma' to start a new run on this channel.\r\n", g_test.ch + 1, sectionName(i), last.ch + 1);
        return;
      }
      last.last = g_test.a;
      if (last.last < last.first) out.printf("(Run %d is backwards: %u down to %u.)\r\n", s.nruns, last.first, last.last);
    }
    changed();
    printSection(out, i);
    return;
  }

  if (cmd == "set" || cmd == "add") {
    int i;
    if (n < 2) { fail(out, "Usage: %s <section> <runs>   e.g. %s waterline 1:90-120, 1:200-170\r\n", cmd.c_str(), cmd.c_str()); return; }
    if (!argSection(t[1], i, out)) return;
    String text = restOfLine(line, 2);
    Run runs[MAX_RUNS_PER_SECTION];
    uint8_t cnt = 0;
    String err;
    SectionCfg& s = g_sect[i];
    uint8_t room = (cmd == "add") ? (uint8_t)(MAX_RUNS_PER_SECTION - s.nruns) : (uint8_t)MAX_RUNS_PER_SECTION;
    if (!parseRuns(text, runs, cnt, room, err)) { fail(out, "Not changed: %s\r\n", err.c_str()); return; }
    if (cmd == "set") s.nruns = 0;
    for (uint8_t k = 0; k < cnt; k++) s.runs[s.nruns++] = runs[k];
    changed();
    printSection(out, i);
    warnRuns(out, i);
    return;
  }

  if (cmd == "clear") {
    int i;
    if (n < 2) { fail(out, "Usage: clear <section>\r\n"); return; }
    if (!argSection(t[1], i, out)) return;
    g_sect[i].nruns = 0;
    changed();
    printSection(out, i);
    return;
  }

  if (cmd == "color" || cmd == "colour") {
    int i; uint32_t c;
    if (n < 3) { fail(out, "Usage: color <section> <RRGGBB>\r\n"); return; }
    if (!argSection(t[1], i, out)) return;
    if (!parseColor(t[2], c)) { fail(out, "Color is RRGGBB hex, e.g. FFA040\r\n"); return; }
    g_sect[i].color = c;
    changed();
    printSection(out, i);
    return;
  }

  if (cmd == "rev") {
    int i;
    if (n < 2) { fail(out, "Usage: rev <section> [on|off]\r\n"); return; }
    if (!argSection(t[1], i, out)) return;
    if (n >= 3) {
      int o = parseOnOff(t[2]);
      if (o < 0) { fail(out, "Usage: rev <section> [on|off]\r\n"); return; }
      g_sect[i].reverse = (uint8_t)o;
    } else g_sect[i].reverse = !g_sect[i].reverse;
    changed();
    printSection(out, i);
    return;
  }

  if (cmd == "chan") {
    uint8_t ch;
    if (n < 4) { fail(out, "Usage: chan <ch> count <n>  |  chan <ch> order <RGB|GRB|...>\r\n"); return; }
    if (!argChannel(t[1], ch, out)) return;
    String what = t[2]; what.toLowerCase();
    if (what == "count") {
      if (!parseNum(t[3], v) || v < 0 || v > MAX_LEDS_PER_CHANNEL) { fail(out, "Count must be 0-%d\r\n", MAX_LEDS_PER_CHANNEL); return; }
      g_chan[ch].count = (uint16_t)v;
      Leds::setCount(ch, (uint16_t)v);
      if (g_test.ch == ch && g_test.a >= v) g_test.a = v ? (uint16_t)(v - 1) : 0;
    } else if (what == "order") {
      int o = parseOrder(t[3]);
      if (o < 0) { fail(out, "Order must be RGB, RBG, GRB, GBR, BRG or BGR\r\n"); return; }
      g_chan[ch].order = (uint8_t)o;
    } else { fail(out, "Usage: chan <ch> count <n>  |  chan <ch> order <RGB|GRB|...>\r\n"); return; }
    changed();
    listChannels(out);
    return;
  }

  if (cmd == "bright" || cmd == "brightness") {
    if (n < 2) { out.printf("Brightness %u (0-255)\r\n", g_set.brightness); return; }
    if (!parseNum(t[1], v) || v < 0 || v > 255) { fail(out, "Brightness is 0-255\r\n"); return; }
    g_set.brightness = (uint8_t)v; changed();
    out.printf("Brightness %u\r\n", g_set.brightness);
    return;
  }

  if (cmd == "speed") {
    if (n < 3) {
      out.printf("Speeds: water %u%%  smoke %u%%  wheel %u%%   (speed <water|smoke|wheel> <0-1000>)\r\n",
                 g_set.waterSpeed, g_set.smokeSpeed, g_set.wheelSpeed);
      return;
    }
    if (!parseNum(t[2], v) || v < 0 || v > 1000) { fail(out, "Speed is 0-1000 (percent)\r\n"); return; }
    String w = t[1]; w.toLowerCase();
    if (w.startsWith("wa")) g_set.waterSpeed = (uint16_t)v;
    else if (w.startsWith("sm")) g_set.smokeSpeed = (uint16_t)v;
    else if (w.startsWith("wh") || w.startsWith("pa")) g_set.wheelSpeed = (uint16_t)v;
    else { fail(out, "speed water|smoke|wheel <percent>\r\n"); return; }
    changed();
    out.printf("Speeds: water %u%%  smoke %u%%  wheel %u%%\r\n", g_set.waterSpeed, g_set.smokeSpeed, g_set.wheelSpeed);
    return;
  }

  if (cmd == "floor") {
    if (n < 2) { out.printf("Spoke floor %u (0-255)\r\n", g_set.spokeFloor); return; }
    if (!parseNum(t[1], v) || v < 0 || v > 255) { fail(out, "Floor is 0-255\r\n"); return; }
    g_set.spokeFloor = (uint8_t)v; changed();
    out.printf("Spoke floor %u\r\n", g_set.spokeFloor);
    return;
  }

  if (cmd == "fade") {
    if (n < 2) { out.printf("Smoke fade %u (0-255)\r\n", g_set.smokeFade); return; }
    if (!parseNum(t[1], v) || v < 0 || v > 255) { fail(out, "Fade is 0-255\r\n"); return; }
    g_set.smokeFade = (uint8_t)v; changed();
    out.printf("Smoke fade %u\r\n", g_set.smokeFade);
    return;
  }

  if (cmd == "wheeldir") {
    if (n >= 2) {
      String w = t[1]; w.toLowerCase();
      if (w.startsWith("norm") || w == "fwd" || w == "forward") g_set.wheelReverse = 0;
      else if (w.startsWith("rev")) g_set.wheelReverse = 1;
      else { fail(out, "Usage: wheeldir [normal|reversed]\r\n"); return; }
    } else g_set.wheelReverse = !g_set.wheelReverse;
    changed();
    out.printf("Paddle wheel direction %s\r\n", g_set.wheelReverse ? "reversed" : "normal");
    return;
  }

  if (cmd == "on" || cmd == "off") {
    g_on = (cmd == "on");
    out.printf("Display %s", g_on ? "on" : "off");
    if (g_on && !displayActive()) out.print(" (but dark until the light sensor sees night)");
    out.print("\r\n");
    return;
  }

  if (cmd == "sensor" || cmd == "light") {
    if (!sensorPresent()) { fail(out, "No light sensor configured (LIGHT_SENSOR_PIN is -1).\r\n"); return; }
    if (n >= 2) {
      String w = t[1]; w.toLowerCase();
      if (w == "on" || w == "off") { g_set.autoDark = (w == "on"); changed(); }
      else if ((w == "dark" || w == "night") && n >= 3 && parseNum(t[2], v) && v >= 0 && v <= 100) { g_set.darkLevel = (uint8_t)v; changed(); }
      else if (w == "day" && n >= 3 && parseNum(t[2], v) && v >= 0 && v <= 100) { g_set.dayLevel = (uint8_t)v; changed(); }
      else if (w == "delay" && n >= 3 && parseNum(t[2], v) && v >= 0 && v <= 3600) { g_set.sensorDelay = (uint16_t)v; changed(); }
      else { fail(out, "Usage: sensor [on|off | dark <0-100> | day <0-100> | delay <seconds>]\r\n"); return; }
      if (g_set.darkLevel >= g_set.dayLevel)
        out.print("Warning: 'dark' should be lower than 'day' (the gap stops it flickering at dusk).\r\n");
    }
    sensorStatus(out);
    return;
  }

  if (cmd == "wifi") {
    String w = n >= 2 ? t[1] : String("");
    w.toLowerCase();
    String val = restOfLine(line, 2);
    if (n < 2) { showNetwork(out); return; }
    if (w == "off") { g_net.staSsid[0] = 0; g_net.staPass[0] = 0; }
    else if (w == "ssid") {
      if (val.length() < 1 || val.length() > 32) { fail(out, "Network name must be 1-32 characters\r\n"); return; }
      strncpy(g_net.staSsid, val.c_str(), sizeof(g_net.staSsid) - 1); g_net.staSsid[sizeof(g_net.staSsid) - 1] = 0;
    } else if (w == "pass" || w == "password") {
      if (val == "none") val = "";
      if (val.length() != 0 && (val.length() < 8 || val.length() > 63)) { fail(out, "Wi-Fi password must be 8-63 characters, or 'none' for an open network\r\n"); return; }
      strncpy(g_net.staPass, val.c_str(), sizeof(g_net.staPass) - 1); g_net.staPass[sizeof(g_net.staPass) - 1] = 0;
    } else { fail(out, "Usage: wifi [ssid <name> | pass <password|none> | off]\r\n"); return; }
    changed();
    out.print("Network setting changed. 'save' then 'reboot' to apply.\r\n");
    return;
  }

  if (cmd == "ap") {
    String w = n >= 2 ? t[1] : String("");
    w.toLowerCase();
    String val = restOfLine(line, 2);
    if (n < 2) { showNetwork(out); return; }
    if (w == "ssid") {
      if (val.length() < 1 || val.length() > 32) { fail(out, "AP name must be 1-32 characters\r\n"); return; }
      strncpy(g_net.apSsid, val.c_str(), sizeof(g_net.apSsid) - 1); g_net.apSsid[sizeof(g_net.apSsid) - 1] = 0;
    } else if (w == "pass" || w == "password") {
      if (val.length() < 8 || val.length() > 63) { fail(out, "AP password must be 8-63 characters (WPA2)\r\n"); return; }
      strncpy(g_net.apPass, val.c_str(), sizeof(g_net.apPass) - 1); g_net.apPass[sizeof(g_net.apPass) - 1] = 0;
    } else { fail(out, "Usage: ap ssid <name>  |  ap pass <8-63 characters>\r\n"); return; }
    changed();
    out.print("Access point setting changed. 'save' then 'reboot' to apply.\r\n");
    return;
  }

  if (cmd == "admin") {
    String w = n >= 2 ? t[1] : String("");
    w.toLowerCase();
    String val = restOfLine(line, 2);
    if (w != "pass" && w != "password") { fail(out, "Usage: admin pass <password>\r\n"); return; }
    if (val.length() < 4 || val.length() > 63) { fail(out, "Admin password must be 4-63 characters\r\n"); return; }
    strncpy(g_net.adminPass, val.c_str(), sizeof(g_net.adminPass) - 1); g_net.adminPass[sizeof(g_net.adminPass) - 1] = 0;
    changed();
    out.print("Admin password changed. 'save' to keep it; the web page uses it right away, OTA after reboot.\r\n");
    return;
  }

  if (cmd == "backup") {
    String w = n >= 2 ? t[1] : String("");
    w.toLowerCase();
    writeBackup(out, w == "secrets" || w == "full");
    return;
  }

  if (cmd == "save") {
    int errs = validate(out, false);
    if (saveConfig()) out.printf("Saved.%s\r\n", errs ? " (There are errors above; saved anyway.)" : "");
    else fail(out, "Save FAILED.\r\n");
    return;
  }
  if (cmd == "reload") { loadConfig(); out.print("Reloaded saved settings.\r\n"); return; }
  if (cmd == "defaults") { loadDefaults(); out.print("Compiled-in show and section defaults loaded (not saved yet; network unchanged).\r\n"); return; }

  if (cmd == "info") {
    out.printf("Steamboat lights %s, %d sections, %d channels\r\n", FW_VERSION, NUM_SECTIONS, NUM_CHANNELS);
    out.printf("%s\r\n", netInfo().c_str());
    out.printf("Uptime %lu s, free heap %lu bytes, display %s%s\r\n", (unsigned long)(millis() / 1000),
               (unsigned long)ESP.getFreeHeap(), g_on ? "on" : "off", g_dirty ? ", UNSAVED changes" : "");
    if (sensorPresent()) sensorStatus(out);
    listChannels(out);
    return;
  }

  if (cmd == "reboot" || cmd == "restart") {
    out.print("Rebooting...\r\n");
    Serial.flush();
    delay(200);
    ESP.restart();
    return;
  }

  fail(out, "Unknown command '%s'. Type 'help'.\r\n", t[0].c_str());
}

// ---------------------------------------------------------------- serial input

void consolePoll() {
  if (!s_rxOk) return;
  while (Serial.available()) {
    int c = Serial.read();
    if (c < 0) break;
    touchActivity();

    if (!s_active) {          // first key opens the console
      activate();
      s_line = ""; s_esc = 0;
      if (c == '\r' || c == '\n') { s_lastCR = (c == '\r'); prompt(); continue; }
    }

    if (g_test.mode == TEST_WALK) {   // any key stops a walk (and is not used for anything else)
      setMode(TEST_PIXEL);
      s_out.print("\r\nStopped at ");
      reportCursor(s_out);
      s_line = "";
      s_lastCR = (c == '\r');
      s_esc = (c == 27) ? 3 : 0;      // swallow the rest of an arrow-key sequence
      prompt();
      continue;
    }
    if (s_esc == 3) { s_esc = (c == '[' || c == 'O') ? 4 : 0; if (s_esc) continue; }
    else if (s_esc == 4) { s_esc = 0; continue; }

    // Arrow keys: ESC [ A/B/C/D
    if (s_esc == 1) { s_esc = (c == '[' || c == 'O') ? 2 : 0; continue; }
    if (s_esc == 2) {
      s_esc = 0;
      if (s_line.length() == 0) {
        s_out.print("\r");
        if (c == 'C') stepCursor(1, s_out);
        else if (c == 'D') stepCursor(-1, s_out);
        else if (c == 'A') stepCursor(10, s_out);
        else if (c == 'B') stepCursor(-10, s_out);
        prompt();
      }
      continue;
    }
    if (c == 27) { s_esc = 1; continue; }

    if (c == '\r' || c == '\n') {
      if (c == '\n' && s_lastCR) { s_lastCR = false; continue; }   // CR LF pair
      s_lastCR = (c == '\r');
      s_out.print("\r\n");
      String line = s_line;
      s_line = "";
      if (line.length()) runCommand(line, s_out);
      prompt();
      continue;
    }
    s_lastCR = false;

    if (c == 8 || c == 127) {   // backspace
      if (s_line.length()) { s_line.remove(s_line.length() - 1); s_out.print("\b \b"); }
      continue;
    }

    // Single-key cursor moves when nothing has been typed yet
    if (s_line.length() == 0 && (c == '+' || c == '=' || c == '-' || c == '_' || c == '[' || c == ']')) {
      s_out.print("\r");
      int d = (c == '+' || c == '=') ? 1 : (c == '-' || c == '_') ? -1 : (c == ']') ? 10 : -10;
      stepCursor(d, s_out);
      prompt();
      continue;
    }

    if (c >= 32 && c < 127 && s_line.length() < 250) {
      s_line += (char)c;
      s_out.write((uint8_t)c);   // echo
    }
  }
}

// True once `ms` have passed since `since`. `now` is read at the top of loop(),
// but commands run after that stamp times with a fresh millis(), so `since` can
// be slightly later than `now`. The signed difference treats that as "no time
// passed" where plain unsigned math would wrap to ~49 days and fire at once.
static bool elapsed(uint32_t now, uint32_t since, uint32_t ms) {
  return (int32_t)(now - since) >= (int32_t)ms;
}

void consoleTick(uint32_t now) {
  if (g_test.mode == TEST_WALK && g_chan[g_test.ch].count && elapsed(now, g_test.lastStep, g_test.walkMs)) {
    g_test.lastStep = now;
    g_test.a = (g_test.a + 1 >= g_chan[g_test.ch].count) ? 0 : g_test.a + 1;
    if (s_active) { s_out.print("\r"); reportCursor(s_out); }
  }
  if ((s_active || g_test.mode != TEST_OFF) && elapsed(now, s_lastActivity, TEST_TIMEOUT_MS)) {
    if (s_active) { s_out.print("\r\n[No input for a while.] "); deactivate(s_out); }
    else g_test.mode = TEST_OFF;
  }
}
