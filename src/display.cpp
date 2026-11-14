// display.cpp - section table, effects, test patterns, saved settings
#include <Preferences.h>
#include <math.h>
#include "app.h"
#include "led_driver.h"

SectionCfg g_sect[NUM_SECTIONS];
ChannelCfg g_chan[NUM_CHANNELS];
Settings   g_set;
TestState  g_test;
bool       g_dirty = false;
bool       g_on = true;
NetCfg     g_net;

static Preferences s_prefs;
static const char* kNs = "steamboat";
static const uint32_t kSettingsVersion = 2;
static const uint32_t kNetVersion = 1;

// ---------------------------------------------------------------- helpers

const char* sectionName(int i)  { return DEFAULT_SECTIONS[i].name; }
Role        sectionRole(int i)  { return DEFAULT_SECTIONS[i].role; }
uint8_t     sectionGroup(int i) { return DEFAULT_SECTIONS[i].group; }

const char* roleName(Role r) {
  switch (r) {
    case ROLE_WATER: return "water";
    case ROLE_SMOKE: return "smoke";
    case ROLE_SPOKE: return "spoke";
    default:         return "static";
  }
}

static const char* kOrderNames[ORDER_COUNT] = { "RGB", "RBG", "GRB", "GBR", "BRG", "BGR" };
const char* orderName(uint8_t o) { return o < ORDER_COUNT ? kOrderNames[o] : "?"; }
int parseOrder(const String& s) {
  String u = s; u.toUpperCase();
  for (int i = 0; i < ORDER_COUNT; i++) if (u == kOrderNames[i]) return i;
  return -1;
}

// ---------------------------------------------------------------- runs

uint16_t runLength(const Run& r) { return (r.first <= r.last ? r.last - r.first : r.first - r.last) + 1; }
static inline uint16_t runLed(const Run& r, uint16_t k) { return r.first <= r.last ? r.first + k : r.first - k; }
static inline uint16_t runMax(const Run& r) { return r.first > r.last ? r.first : r.last; }

uint16_t sectionCount(int i) {
  uint32_t n = 0;
  for (uint8_t r = 0; r < g_sect[i].nruns; r++) n += runLength(g_sect[i].runs[r]);
  return n > 65535 ? 65535 : (uint16_t)n;
}

bool sectionOnChannel(int i, uint8_t ch) {
  for (uint8_t r = 0; r < g_sect[i].nruns; r++) if (g_sect[i].runs[r].ch == ch) return true;
  return false;
}

// Call f(ch, led, idx) for every LED of section i in listed order (idx 0..count-1)
template <typename F>
static void forEachLed(int i, F f) {
  uint16_t idx = 0;
  const SectionCfg& s = g_sect[i];
  for (uint8_t r = 0; r < s.nruns; r++) {
    const Run& run = s.runs[r];
    uint16_t len = runLength(run);
    for (uint16_t k = 0; k < len; k++) f(run.ch, runLed(run, k), idx++);
  }
}

static bool parseUInt(const String& s, long& v) {
  if (s.length() == 0) return false;
  for (size_t k = 0; k < s.length(); k++) if (!isdigit((unsigned char)s[k])) return false;
  v = s.toInt();
  return true;
}

bool parseRuns(const String& text, Run* runs, uint8_t& n, uint8_t maxRuns, String& err) {
  String t;
  for (size_t i = 0; i < text.length(); i++) if (text[i] != ' ' && text[i] != '\t') t += text[i];  // spaces ignored
  n = 0;
  int ch = -1;
  int start = 0;
  while (start < (int)t.length()) {
    int comma = t.indexOf(',', start);
    if (comma < 0) comma = t.length();
    String item = t.substring(start, comma);
    start = comma + 1;
    if (item.length() == 0) continue;
    if (n >= maxRuns) { err = "more than " + String((int)maxRuns) + " runs (MAX_RUNS_PER_SECTION)"; return false; }
    int colon = item.indexOf(':');
    if (colon >= 0) {
      long c;
      if (!parseUInt(item.substring(0, colon), c) || c < 1 || c > NUM_CHANNELS) {
        err = "bad channel in '" + item + "' (1-" + String(NUM_CHANNELS) + ")"; return false;
      }
      ch = (int)c - 1;
      item = item.substring(colon + 1);
    }
    if (ch < 0) { err = "'" + item + "' needs a channel, like 1:" + item; return false; }
    int dash = item.indexOf('-');
    long a, b;
    if (dash < 0) {
      if (!parseUInt(item, a)) { err = "bad LED number '" + item + "'"; return false; }
      b = a;
    } else if (!parseUInt(item.substring(0, dash), a) || !parseUInt(item.substring(dash + 1), b)) {
      err = "bad range '" + item + "' (use first-last)"; return false;
    }
    if (a >= MAX_LEDS_PER_CHANNEL || b >= MAX_LEDS_PER_CHANNEL) {
      err = "LED number too big in '" + item + "' (max " + String(MAX_LEDS_PER_CHANNEL - 1) + ")"; return false;
    }
    runs[n].ch = (uint8_t)ch; runs[n].pad = 0; runs[n].first = (uint16_t)a; runs[n].last = (uint16_t)b;
    n++;
  }
  return true;
}

String formatRuns(int i) {
  String o;
  const SectionCfg& s = g_sect[i];
  for (uint8_t r = 0; r < s.nruns; r++) {
    const Run& run = s.runs[r];
    if (r) o += ", ";
    o += String((int)run.ch + 1) + ":" + String((int)run.first);
    if (run.last != run.first) o += "-" + String((int)run.last);
  }
  return o;
}

static String normName(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c)) o += (char)tolower((unsigned char)c);
  }
  return o;
}

// Section by number (1-based, as listed) or by name: exact, then unique
// prefix, then unique substring; spaces and case ignored. -1 none, -2 ambiguous.
int findSection(const String& id) {
  if (id.length() == 0) return -1;
  bool numeric = true;
  for (size_t i = 0; i < id.length(); i++) if (!isdigit((unsigned char)id[i])) numeric = false;
  if (numeric) {
    int n = id.toInt();
    return (n >= 1 && n <= NUM_SECTIONS) ? n - 1 : -1;
  }
  String want = normName(id);
  if (want.length() == 0) return -1;
  for (int i = 0; i < NUM_SECTIONS; i++) if (normName(sectionName(i)) == want) return i;
  int found = -1, hits = 0;
  for (int i = 0; i < NUM_SECTIONS; i++)
    if (normName(sectionName(i)).startsWith(want)) { found = i; hits++; }
  if (hits == 1) return found;
  if (hits > 1) return -2;
  for (int i = 0; i < NUM_SECTIONS; i++)
    if (normName(sectionName(i)).indexOf(want) >= 0) { found = i; hits++; }
  if (hits == 1) return found;
  return hits > 1 ? -2 : -1;
}

int sectionAt(uint8_t ch, uint16_t led, uint16_t* offset) {
  for (int i = NUM_SECTIONS - 1; i >= 0; i--) {   // last one drawn wins
    const SectionCfg& s = g_sect[i];
    uint16_t idx = 0;
    for (uint8_t r = 0; r < s.nruns; r++) {
      const Run& run = s.runs[r];
      uint16_t len = runLength(run);
      uint16_t lo = run.first < run.last ? run.first : run.last;
      if (run.ch == ch && led >= lo && led <= runMax(run)) {
        uint16_t pos = idx + (run.first <= run.last ? led - run.first : run.first - led);
        if (offset) *offset = s.reverse ? sectionCount(i) - 1 - pos : pos;
        return i;
      }
      idx += len;
    }
  }
  return -1;
}

int numGroups(Role r) {
  int g = 0;
  for (int i = 0; i < NUM_SECTIONS; i++)
    if (sectionRole(i) == r && sectionGroup(i) + 1 > g) g = sectionGroup(i) + 1;
  return g;
}

bool testUsesChannel(uint8_t ch) {
  switch (g_test.mode) {
    case TEST_PIXEL: case TEST_RANGE: case TEST_RULER:
    case TEST_FILL:  case TEST_RGB:   case TEST_WALK:
      return g_test.ch == ch;
    case TEST_SECTION:
      return g_test.section >= 0 && sectionOnChannel(g_test.section, ch);
    case TEST_ENDS:
      for (int i = 0; i < NUM_SECTIONS; i++) if (sectionOnChannel(i, ch)) return true;
      return false;
    default:
      return false;
  }
}

// ---------------------------------------------------------------- storage

// Signature of the section table layout; saved sections are only reused if
// the compiled table still has the same names in the same order.
static uint32_t tableSignature() {
  uint32_t h = 2166136261u;
  for (int i = 0; i < NUM_SECTIONS; i++) {
    for (const char* p = sectionName(i); *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
    h ^= 0xFF; h *= 16777619u;
  }
  h ^= NUM_CHANNELS; h *= 16777619u;
  h ^= sizeof(SectionCfg); h *= 16777619u;
  h ^= MAX_RUNS_PER_SECTION; h *= 16777619u;
  return h;
}

static void defaultSettings() {
  g_set.brightness     = DEFAULT_BRIGHTNESS;
  g_set.spokeFloor     = DEFAULT_SPOKE_FLOOR;
  g_set.smokeFade      = DEFAULT_SMOKE_FADE;
  g_set.wheelReverse   = 0;
  g_set.waterSpeed     = DEFAULT_WATER_SPEED;
  g_set.smokeSpeed     = DEFAULT_SMOKE_SPEED;
  g_set.wheelSpeed     = DEFAULT_WHEEL_SPEED;
  g_set.testBackground = 1;
  g_set.autoDark       = DEFAULT_AUTO_DARK ? 1 : 0;
  g_set.darkLevel      = DEFAULT_DARK_LEVEL;
  g_set.dayLevel       = DEFAULT_DAY_LEVEL;
  g_set.sensorDelay    = DEFAULT_SENSOR_DELAY;
}

static void defaultLayout() {
  for (int c = 0; c < NUM_CHANNELS; c++) {
    g_chan[c].count = DEFAULT_CHANNEL_LEDS[c] > MAX_LEDS_PER_CHANNEL ? MAX_LEDS_PER_CHANNEL : DEFAULT_CHANNEL_LEDS[c];
    g_chan[c].order = DEFAULT_CHANNEL_ORDER[c];
    g_chan[c].pad = 0;
  }
  memset(g_sect, 0, sizeof(SectionCfg) * NUM_SECTIONS);
  for (int i = 0; i < NUM_SECTIONS; i++) {
    const SectionDef& d = DEFAULT_SECTIONS[i];
    g_sect[i].color = d.color & 0xFFFFFF;
    String err;
    if (!parseRuns(d.leds ? d.leds : "", g_sect[i].runs, g_sect[i].nruns, MAX_RUNS_PER_SECTION, err)) {
      Serial.printf("config.h section '%s': %s\r\n", d.name, err.c_str());
      g_sect[i].nruns = 0;
    }
  }
}

static void copyStr(char* dst, size_t size, const char* src) {
  strncpy(dst, src ? src : "", size - 1);
  dst[size - 1] = 0;
}

static void defaultNet() {
  memset(&g_net, 0, sizeof(g_net));
  copyStr(g_net.staSsid, sizeof(g_net.staSsid), WIFI_SSID);
  copyStr(g_net.staPass, sizeof(g_net.staPass), WIFI_PASSWORD);
  copyStr(g_net.apSsid, sizeof(g_net.apSsid), AP_SSID);
  copyStr(g_net.apPass, sizeof(g_net.apPass), AP_PASSWORD);
  copyStr(g_net.adminPass, sizeof(g_net.adminPass), ADMIN_PASSWORD);
}

static void applyCounts() {
  for (int c = 0; c < NUM_CHANNELS; c++) Leds::setCount(c, g_chan[c].count);
}

void loadDefaults() {
  defaultSettings();
  defaultLayout();
  applyCounts();
  g_dirty = true;
}

void loadConfig() {
  defaultSettings();
  defaultLayout();
  defaultNet();
  s_prefs.begin(kNs, true);
  if (s_prefs.getUInt("netv", 0) == kNetVersion && s_prefs.getBytesLength("net") == sizeof(g_net)) {
    s_prefs.getBytes("net", &g_net, sizeof(g_net));
    g_net.staSsid[sizeof(g_net.staSsid) - 1] = 0;  g_net.staPass[sizeof(g_net.staPass) - 1] = 0;
    g_net.apSsid[sizeof(g_net.apSsid) - 1] = 0;    g_net.apPass[sizeof(g_net.apPass) - 1] = 0;
    g_net.adminPass[sizeof(g_net.adminPass) - 1] = 0;
  }
  if (s_prefs.getUInt("setv", 0) == kSettingsVersion &&
      s_prefs.getBytesLength("set") == sizeof(g_set)) {
    s_prefs.getBytes("set", &g_set, sizeof(g_set));
  }
  if (s_prefs.getUInt("sig", 0) == tableSignature()) {
    if (s_prefs.getBytesLength("chan") == sizeof(g_chan)) s_prefs.getBytes("chan", g_chan, sizeof(g_chan));
    if (s_prefs.getBytesLength("sect") == sizeof(SectionCfg) * NUM_SECTIONS)
      s_prefs.getBytes("sect", g_sect, sizeof(SectionCfg) * NUM_SECTIONS);
  }
  s_prefs.end();
  // Sanity
  for (int c = 0; c < NUM_CHANNELS; c++) {
    if (g_chan[c].count > MAX_LEDS_PER_CHANNEL) g_chan[c].count = MAX_LEDS_PER_CHANNEL;
    if (g_chan[c].order >= ORDER_COUNT) g_chan[c].order = ORDER_RGB;
  }
  for (int i = 0; i < NUM_SECTIONS; i++) {
    SectionCfg& s = g_sect[i];
    if (s.nruns > MAX_RUNS_PER_SECTION) s.nruns = 0;
    for (uint8_t r = 0; r < s.nruns; r++)
      if (s.runs[r].ch >= NUM_CHANNELS || runMax(s.runs[r]) >= MAX_LEDS_PER_CHANNEL) { s.nruns = 0; break; }
  }
  applyCounts();
  g_dirty = false;
}

bool saveConfig() {
  if (!s_prefs.begin(kNs, false)) return false;
  bool ok = true;
  size_t sectBytes = sizeof(SectionCfg) * NUM_SECTIONS;
  ok &= s_prefs.putUInt("setv", kSettingsVersion) > 0;
  ok &= s_prefs.putBytes("set", &g_set, sizeof(g_set)) == sizeof(g_set);
  ok &= s_prefs.putUInt("sig", tableSignature()) > 0;
  ok &= s_prefs.putBytes("chan", g_chan, sizeof(g_chan)) == sizeof(g_chan);
  ok &= s_prefs.putBytes("sect", g_sect, sectBytes) == sectBytes;
  ok &= s_prefs.putUInt("netv", kNetVersion) > 0;
  ok &= s_prefs.putBytes("net", &g_net, sizeof(g_net)) == sizeof(g_net);
  s_prefs.end();
  applyCounts();
  if (ok) g_dirty = false;
  return ok;
}

// ---------------------------------------------------------------- validation

int validate(Print& out, bool verbose) {
  int errors = 0, warnings = 0;
  static uint8_t owner[MAX_LEDS_PER_CHANNEL];

  for (int i = 0; i < NUM_SECTIONS; i++) {
    const SectionCfg& s = g_sect[i];
    if (s.nruns == 0) { out.printf("  warning: #%d %s has no LEDs\r\n", i + 1, sectionName(i)); warnings++; continue; }
    for (uint8_t r = 0; r < s.nruns; r++) {
      const Run& run = s.runs[r];
      if (runMax(run) >= g_chan[run.ch].count) {
        out.printf("  ERROR: #%d %s run %d (%d:%u-%u) goes past the end of channel %d (%u LEDs, 0-%d)\r\n",
                   i + 1, sectionName(i), r + 1, run.ch + 1, run.first, run.last, run.ch + 1,
                   g_chan[run.ch].count, g_chan[run.ch].count - 1);
        errors++;
      }
    }
  }
  for (int c = 0; c < NUM_CHANNELS; c++) {
    uint16_t n = g_chan[c].count;
    memset(owner, 0xFF, n);
    for (int i = 0; i < NUM_SECTIONS; i++) {
      int reported = -1;
      forEachLed(i, [&](uint8_t ch, uint16_t led, uint16_t) {
        if (ch != c || led >= n) return;
        if (owner[led] != 0xFF && owner[led] != reported) {
          if (owner[led] == i)
            out.printf("  warning: #%d %s uses ch%d LED %u more than once\r\n", i + 1, sectionName(i), c + 1, led);
          else
            out.printf("  warning: #%d %s overlaps #%d %s at ch%d LED %u\r\n",
                       i + 1, sectionName(i), owner[led] + 1, sectionName(owner[led]), c + 1, led);
          reported = owner[led];
          warnings++;
        }
        owner[led] = (uint8_t)i;
      });
    }
    if (verbose) {
      uint32_t k = 0;
      while (k < n) {
        if (owner[k] != 0xFF) { k++; continue; }
        uint32_t a = k;
        while (k < n && owner[k] == 0xFF) k++;
        out.printf("  note: ch%d LEDs %lu-%lu (%lu) are not in any section\r\n",
                   c + 1, (unsigned long)a, (unsigned long)(k - 1), (unsigned long)(k - a));
      }
    }
  }
  if (verbose) {
    if (errors == 0 && warnings == 0) out.print("  Section table OK.\r\n");
    else out.printf("  %d error(s), %d warning(s).\r\n", errors, warnings);
  }
  return errors;
}

// ---------------------------------------------------------------- drawing

static inline void setPx(uint8_t ch, uint16_t i, uint32_t c, uint8_t level = 255) {
  if (ch >= NUM_CHANNELS || i >= g_chan[ch].count) return;
  uint8_t* p = Leds::buffer(ch) + i * 3;
  uint16_t l = (uint16_t)level + 1;
  p[0] = (uint8_t)((((c >> 16) & 0xFF) * l) >> 8);
  p[1] = (uint8_t)((((c >> 8) & 0xFF) * l) >> 8);
  p[2] = (uint8_t)(((c & 0xFF) * l) >> 8);
}

static void fillSection(int s, uint32_t c, uint8_t level = 255) {
  forEachLed(s, [&](uint8_t ch, uint16_t led, uint16_t) { setPx(ch, led, c, level); });
}

// Mark where each run starts (green) and ends (red), in animation order.
static void markRunEnds(int s) {
  const SectionCfg& sc = g_sect[s];
  uint32_t startC = sc.reverse ? 0xFF0000 : 0x00FF00;
  uint32_t endC   = sc.reverse ? 0x00FF00 : 0xFF0000;
  for (uint8_t r = 0; r < sc.nruns; r++) {
    const Run& run = sc.runs[r];
    if (run.first != run.last) setPx(run.ch, run.last, endC);
    setPx(run.ch, run.first, startC);
  }
}

static inline float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static inline float ease(float x) { x = clamp01(x); return x * x * (3.f - 2.f * x); }
static inline uint8_t lvl(float x) { return (uint8_t)(clamp01(x) * 255.f + 0.5f); }

static uint32_t lerpColor(uint32_t a, uint32_t b, float t) {
  t = clamp01(t);
  int r = ((a >> 16) & 0xFF) + (int)((((int)((b >> 16) & 0xFF)) - (int)((a >> 16) & 0xFF)) * t);
  int g = ((a >> 8) & 0xFF)  + (int)((((int)((b >> 8) & 0xFF))  - (int)((a >> 8) & 0xFF))  * t);
  int bl = (a & 0xFF)        + (int)((((int)(b & 0xFF))         - (int)(a & 0xFF))         * t);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static uint32_t waterColor(float u) {
  // u 0..1 -> blend through WATER_COLORS; bias toward darker so light caps are rarer
  u = clamp01(u);
  u = powf(u, 1.4f);
  float x = u * 3.f;
  int i = (int)x;
  if (i >= 3) return WATER_COLORS[3];
  return lerpColor(WATER_COLORS[i], WATER_COLORS[i + 1], x - i);
}

// Animation clocks (advance with speed so changing speed never jumps)
static uint32_t s_lastNow = 0;
static double   s_waterT = 0;     // seconds
static double   s_smokeMs = 0;
static double   s_wheelSteps = 0;

static void advanceClocks(uint32_t now) {
  uint32_t dt = s_lastNow ? now - s_lastNow : 0;
  if (dt > 200) dt = 200;
  s_lastNow = now;
  s_waterT     += dt / 1000.0 * g_set.waterSpeed / 100.0;
  s_smokeMs    += dt * g_set.smokeSpeed / 100.0;
  s_wheelSteps += (double)dt / WHEEL_STEP_MS * g_set.wheelSpeed / 100.0;
  if (s_waterT > 1.0e6) s_waterT = 0;   // after ~11 days; one tiny jump
}

// Water flows from the first LED of the first run toward the last LED of the
// last run (or the other way if the section is reversed), however the runs are
// wired, because position comes from the listed order, not the LED number.
static void drawWater(int s) {
  const double twoPi = 6.283185307179586;
  float p1 = (float)fmod(s_waterT * 2.2, twoPi);
  float p2 = (float)fmod(s_waterT * 1.3, twoPi);
  float p3 = (float)fmod(s_waterT * 3.1, twoPi);
  uint16_t total = sectionCount(s);
  bool rev = g_sect[s].reverse;
  forEachLed(s, [&](uint8_t ch, uint16_t led, uint16_t idx) {
    float x = rev ? (float)(total - 1 - idx) : (float)idx;
    float w = sinf(x * 0.55f - p1) + 0.7f * sinf(x * 0.23f - p2 + 1.7f) + 0.4f * sinf(x * 1.13f - p3 + 4.0f);
    setPx(ch, led, waterColor((w + 2.1f) / 4.2f));
  });
}

static float smokeLevel(int group, int groups) {
  float cycle = (groups - 1) * (float)SMOKE_STAGGER_MS + SMOKE_RISE_MS + SMOKE_HOLD_MS + SMOKE_FALL_MS + SMOKE_PAUSE_MS;
  float ph = (float)fmod(s_smokeMs, (double)cycle);
  float t = ph - group * (float)SMOKE_STAGGER_MS;
  float e;
  if (t < 0)                                            e = 0;
  else if (t < SMOKE_RISE_MS)                           e = ease(t / SMOKE_RISE_MS);
  else if (t < SMOKE_RISE_MS + SMOKE_HOLD_MS)           e = 1;
  else if (t < SMOKE_RISE_MS + SMOKE_HOLD_MS + SMOKE_FALL_MS)
    e = 1.f - ease((t - SMOKE_RISE_MS - SMOKE_HOLD_MS) / SMOKE_FALL_MS);
  else                                                  e = 0;
  float fade = 1.f - group * (g_set.smokeFade / 255.f);
  if (fade < 0.1f) fade = 0.1f;
  return e * e * fade;   // squared envelope ~ gamma, so fades look even
}

static float spokeLevel(int group, int groups) {
  double pos = fmod(s_wheelSteps, (double)groups);
  int cur = (int)pos;
  float f = (float)(pos - cur);
  float fadeStart = 1.f - WHEEL_FADE;
  float x = (f < fadeStart || WHEEL_FADE <= 0) ? 0.f : ease((f - fadeStart) / WHEEL_FADE);
  int g = g_set.wheelReverse ? (groups - 1 - group) : group;
  float level = 0;
  if (g == cur) level = 1.f - x;
  else if (g == (cur + 1) % groups) level = x;
  float floor = g_set.spokeFloor / 255.f;
  return floor + (1.f - floor) * level;
}

static void drawShow(uint8_t dim) {
  int smokeGroups = numGroups(ROLE_SMOKE);
  int spokeGroups = numGroups(ROLE_SPOKE);
  for (int s = 0; s < NUM_SECTIONS; s++) {
    if (g_sect[s].nruns == 0) continue;
    switch (sectionRole(s)) {
      case ROLE_WATER:
        if (dim == 255) drawWater(s); else fillSection(s, WATER_COLORS[2], dim);
        break;
      case ROLE_SMOKE:
        if (dim == 255) fillSection(s, g_sect[s].color, lvl(smokeLevel(sectionGroup(s), smokeGroups)));
        else fillSection(s, g_sect[s].color, dim);
        break;
      case ROLE_SPOKE:
        if (dim == 255) fillSection(s, g_sect[s].color, lvl(spokeLevel(sectionGroup(s), spokeGroups)));
        else fillSection(s, g_sect[s].color, dim);
        break;
      default:
        fillSection(s, g_sect[s].color, dim);
        break;
    }
  }
}

static void clearChannel(uint8_t c) { memset(Leds::buffer(c), 0, g_chan[c].count * 3); }

static void drawTest(uint32_t now) {
  const uint8_t bgDim = 22;
  bool blink = (now / 300) & 1;
  if (g_set.testBackground && g_test.mode != TEST_BLANK) drawShow(bgDim);
  uint8_t ch = g_test.ch < NUM_CHANNELS ? g_test.ch : 0;
  uint16_t n = g_chan[ch].count;

  switch (g_test.mode) {
    case TEST_PIXEL:
    case TEST_WALK:
      if (g_set.testBackground) setPx(ch, g_test.a, blink ? 0xFFFFFF : 0x000000);
      else setPx(ch, g_test.a, 0xFFFFFF);
      break;
    case TEST_RANGE:
      for (uint32_t k = g_test.a; k <= g_test.b && k < n; k++) setPx(ch, k, 0xFFFFFF);
      break;
    case TEST_SECTION:
      if (g_test.section >= 0 && g_sect[g_test.section].nruns) {
        int s = g_test.section;
        const SectionCfg& sc = g_sect[s];
        uint16_t total = sectionCount(s);
        // A white dot with a short tail travels through the section in
        // animation order, so you can see the order and direction of the runs.
        uint16_t pos = (uint16_t)((now / 70) % total);
        fillSection(s, sc.color, 70);
        markRunEnds(s);
        forEachLed(s, [&](uint8_t c, uint16_t led, uint16_t idx) {
          uint16_t x = sc.reverse ? total - 1 - idx : idx;
          uint16_t d = (uint16_t)((pos + total - x) % total);
          if (d == 0) setPx(c, led, 0xFFFFFF);
          else if (d == 1) setPx(c, led, 0xFFFFFF, 110);
          else if (d == 2) setPx(c, led, 0xFFFFFF, 40);
        });
      }
      break;
    case TEST_ENDS:
      for (int c = 0; c < NUM_CHANNELS; c++) clearChannel(c);
      for (int i = 0; i < NUM_SECTIONS; i++) {
        if (!g_sect[i].nruns) continue;
        fillSection(i, g_sect[i].color, 60);
        markRunEnds(i);
      }
      break;
    case TEST_RULER:
      clearChannel(ch);
      for (uint16_t k = 0; k < n; k++) {
        uint32_t c;
        if (k == 0)              c = 0x00FF00;   // green: LED 0
        else if (k % 100 == 0)   c = 0xFF00FF;   // magenta: every 100th
        else if (k % 10 == 0)    c = 0xFF0000;   // red: every 10th
        else if (k % 5 == 0)     c = 0x0000FF;   // blue: every 5th
        else                     c = 0x202020;   // dim white
        setPx(ch, k, c);
      }
      break;
    case TEST_FILL:
      for (uint16_t k = 0; k < n; k++) setPx(ch, k, g_test.color);
      break;
    case TEST_RGB:
      for (uint16_t k = 0; k < n; k++) setPx(ch, k, k % 3 == 0 ? 0xFF0000 : (k % 3 == 1 ? 0x00FF00 : 0x0000FF));
      break;
    default:
      break;
  }
}

void displayRender(uint32_t now) {
  advanceClocks(now);
  for (int c = 0; c < NUM_CHANNELS; c++) clearChannel(c);
  if (!displayActive() && g_test.mode == TEST_OFF) return;
  if (g_test.mode == TEST_OFF) drawShow(255);
  else drawTest(now);
}
