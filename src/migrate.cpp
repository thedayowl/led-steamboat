// migrate.cpp - move a board onto this firmware's partition layout without USB.
//
// A board running WLED can install this firmware through WLED's own update
// page: the image (about 1.3 MB) fits WLED's 1.5 MB app slots. WLED's
// partition table stays behind, though, so on the first boot this code
// rewrites it to ours (min_spiffs.csv: two 1.875 MB app slots):
//
//  1. Check that it is safe: classic ESP32, at least 4 MB of flash, NVS and
//     otadata at the standard places, running from an OTA app slot.
//  2. Copy WLED's saved Wi-Fi network and password (LittleFS /cfg.json and
//     /wsec.json) into our settings, if no home network is set yet.
//  3. If the image isn't running at 0x10000 (where our app0 starts), copy it
//     there (over WLED) and verify the copy. Power loss here is harmless: the
//     old table still boots the running copy, which starts over.
//  4. Write our partition table and erase otadata, so the bootloader starts
//     app0. Only a power cut during the ~0.1 s table write needs USB to fix.
//  5. Restart.
//
// The normal flash API refuses to write the partition table (the core is
// built with CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS), so the writes below
// briefly swap in a copy of the flash driver's OS hooks without that check.
#include "app.h"
#include "led_driver.h"
#include <LittleFS.h>
#include <esp_flash.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <esp_rom_md5.h>
#include <memory>
#include <new>

static const uint32_t kTableAddr = 0x8000;
static const size_t   kTableLen  = 0xC00;   // 3 KB, padded with 0xFF
static const uint32_t kApp0      = 0x10000;
static const uint32_t kSector    = 0x1000;

struct PartEnt { uint8_t type, sub; uint32_t off, size; const char* label; };

// Our layout: the Arduino core's min_spiffs.csv (see platformio.ini).
static const PartEnt kLayout[] = {
  { 0x01, 0x02, 0x9000,   0x5000,   "nvs" },
  { 0x01, 0x00, 0xE000,   0x2000,   "otadata" },
  { 0x00, 0x10, 0x10000,  0x1E0000, "app0" },
  { 0x00, 0x11, 0x1F0000, 0x1E0000, "app1" },
  { 0x01, 0x82, 0x3D0000, 0x20000,  "spiffs" },
  { 0x01, 0x03, 0x3F0000, 0x10000,  "coredump" },
};
static const int kLayoutN = sizeof(kLayout) / sizeof(kLayout[0]);

static void putLe32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

// The binary table exactly as gen_esp32part.py writes it: 32-byte entries,
// then an MD5 entry over them, then 0xFF padding.
static void buildTable(uint8_t* t) {
  memset(t, 0xFF, kTableLen);
  uint8_t* p = t;
  for (int i = 0; i < kLayoutN; i++, p += 32) {
    memset(p, 0, 32);
    p[0] = 0xAA; p[1] = 0x50;
    p[2] = kLayout[i].type; p[3] = kLayout[i].sub;
    putLe32(p + 4, kLayout[i].off);
    putLe32(p + 8, kLayout[i].size);
    strncpy((char*)p + 12, kLayout[i].label, 16);
  }
  p[0] = 0xEB; p[1] = 0xEB;                  // MD5 entry: EB EB, 14 x FF, digest
  memset(p + 2, 0xFF, 14);
  md5_context_t ctx;
  esp_rom_md5_init(&ctx);
  esp_rom_md5_update(&ctx, t, p - t);
  esp_rom_md5_final(p + 16, &ctx);
}

struct TableInfo {
  bool     valid = false;
  int      n = 0;
  uint32_t app0Off = 0, app0Size = 0, app1Off = 0, app1Size = 0;
  bool     nvsOk = false, otadataOk = false;
};

static TableInfo parseTable(const uint8_t* t) {
  TableInfo ti;
  for (const uint8_t* p = t; p + 32 <= t + kTableLen; p += 32) {
    if (p[0] != 0xAA || p[1] != 0x50) break;
    ti.n++;
    uint8_t type = p[2], sub = p[3];
    uint32_t off = le32(p + 4), size = le32(p + 8);
    if (type == 0x01 && sub == 0x02 && off == 0x9000 && size == 0x5000) ti.nvsOk = true;
    if (type == 0x01 && sub == 0x00 && off == 0xE000 && size == 0x2000) ti.otadataOk = true;
    if (type == 0x00 && sub == 0x10) { ti.app0Off = off; ti.app0Size = size; }
    if (type == 0x00 && sub == 0x11) { ti.app1Off = off; ti.app1Size = size; }
  }
  ti.valid = ti.n > 0;
  return ti;
}

static bool isOurLayout(const TableInfo& ti) {
  return ti.app0Off == kLayout[2].off && ti.app0Size == kLayout[2].size &&
         ti.app1Off == kLayout[3].off && ti.app1Size == kLayout[3].size;
}

// ---------------------------------------------------------------- raw flash access

static esp_err_t allowAll(void*, size_t, size_t) { return ESP_OK; }

// Run fn with the flash driver's write protection for the bootloader /
// partition table / running app lifted. Only used here, with the checks above.
template <typename F> static esp_err_t unprotected(F fn) {
  static esp_flash_os_functions_t hooks;
  esp_flash_t* chip = esp_flash_default_chip;
  const esp_flash_os_functions_t* saved = chip->os_func;
  hooks = *saved;
  hooks.region_protected = allowAll;
  chip->os_func = &hooks;
  esp_err_t e = fn(chip);
  chip->os_func = saved;
  return e;
}

static esp_err_t eraseWrite(uint32_t addr, const uint8_t* data, size_t len) {
  return unprotected([&](esp_flash_t* chip) {
    size_t eraseLen = (len + kSector - 1) & ~(kSector - 1);
    esp_err_t e = esp_flash_erase_region(chip, addr, eraseLen);
    if (e == ESP_OK) e = esp_flash_write(chip, data, addr, len);
    return e;
  });
}

static bool flashEquals(uint32_t addr, const uint8_t* data, size_t len, uint8_t* scratch) {
  return esp_flash_read(nullptr, scratch, addr, len) == ESP_OK && memcmp(scratch, data, len) == 0;
}

// ---------------------------------------------------------------- WLED Wi-Fi settings

// Value of the first "key":"..." after position from, JSON escapes decoded.
static bool jsonStr(const String& doc, const char* key, int from, String& out) {
  String k = String("\"") + key + "\"";
  int p = doc.indexOf(k, from);
  if (p < 0) return false;
  p += k.length();
  while (p < (int)doc.length() && (doc[p] == ' ' || doc[p] == ':' || doc[p] == '\t' || doc[p] == '\r' || doc[p] == '\n')) p++;
  if (p >= (int)doc.length() || doc[p] != '"') return false;
  out = "";
  for (p++; p < (int)doc.length(); p++) {
    char c = doc[p];
    if (c == '"') return true;
    if (c != '\\') { out += c; continue; }
    if (++p >= (int)doc.length()) return false;
    c = doc[p];
    if (c == 'n') out += '\n';
    else if (c == 't') out += '\t';
    else if (c == 'r') out += '\r';
    else if (c == 'b') out += '\b';
    else if (c == 'f') out += '\f';
    else if (c == 'u' && p + 4 < (int)doc.length()) {
      uint32_t u = strtoul(doc.substring(p + 1, p + 5).c_str(), nullptr, 16);
      p += 4;
      if (u < 0x80) out += (char)u;
      else if (u < 0x800) { out += (char)(0xC0 | (u >> 6)); out += (char)(0x80 | (u & 0x3F)); }
      else { out += (char)(0xE0 | (u >> 12)); out += (char)(0x80 | ((u >> 6) & 0x3F)); out += (char)(0x80 | (u & 0x3F)); }
    } else out += c;   // \" \\ \/
  }
  return false;
}

static String readFile(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) return String();
  String s = f.readString();
  f.close();
  return s;
}

// WLED keeps the first network's name in /cfg.json (nw.ins[0].ssid) and its
// password in /wsec.json (nw.ins[0].psk). Mount read-only-ish: never format.
static void carryOverWledWifi(Print& out) {
  if (g_net.staSsid[0]) { out.print("Migrate: home Wi-Fi already set; not importing WLED's.\r\n"); return; }
  if (!LittleFS.begin(false, "/wled", 4, "spiffs")) { out.print("Migrate: no WLED file system found; Wi-Fi not imported.\r\n"); return; }
  String cfg = readFile("/cfg.json"), sec = readFile("/wsec.json");
  LittleFS.end();
  String ssid, pass;
  int c = cfg.indexOf("\"nw\""), s = sec.indexOf("\"nw\"");
  if (c < 0 || !jsonStr(cfg, "ssid", cfg.indexOf("\"ins\"", c), ssid) || ssid.length() == 0) {
    out.print("Migrate: WLED has no home Wi-Fi saved.\r\n");
    return;
  }
  if (s >= 0) jsonStr(sec, "psk", sec.indexOf("\"ins\"", s), pass);
  if (ssid.length() >= sizeof(g_net.staSsid) || pass.length() >= sizeof(g_net.staPass)) {
    out.print("Migrate: WLED's Wi-Fi name or password is too long to import.\r\n");
    return;
  }
  strlcpy(g_net.staSsid, ssid.c_str(), sizeof(g_net.staSsid));
  strlcpy(g_net.staPass, pass.c_str(), sizeof(g_net.staPass));
  if (saveConfig()) out.printf("Migrate: imported WLED's Wi-Fi network '%s'.\r\n", g_net.staSsid);
  else out.print("Migrate: could not save the imported Wi-Fi settings.\r\n");
}

// ---------------------------------------------------------------- migration

// Checks; on success sets runAddr / imageLen. Returns "" if migration is
// possible, otherwise the reason it isn't (or "ok" when nothing is needed).
static String assess(const TableInfo& ti, uint32_t& runAddr, uint32_t& imageLen) {
  if (!ti.valid) return "the partition table can't be read";
  if (isOurLayout(ti)) return "ok";
#if !CONFIG_IDF_TARGET_ESP32
  return "only classic ESP32 boards are supported";
#endif
  uint32_t flashSize = 0;
  if (esp_flash_get_size(nullptr, &flashSize) != ESP_OK || flashSize < 0x400000) return "the flash is smaller than 4 MB";
  if (!ti.nvsOk || !ti.otadataOk) return "NVS / otadata aren't at the standard addresses";
  const esp_partition_t* run = esp_ota_get_running_partition();
  if (!run || run->type != ESP_PARTITION_TYPE_APP || run->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
      run->subtype > ESP_PARTITION_SUBTYPE_APP_OTA_15)
    return "not running from an OTA app slot";
  runAddr = run->address;
  imageLen = ESP.getSketchSize();
  if (imageLen == 0 || imageLen > kLayout[2].size) return "the running image is too big for the new app slot";
  if (runAddr != kApp0 && runAddr < kApp0 + ((imageLen + kSector - 1) & ~(kSector - 1)))
    return "the running image would overlap its own copy";
  return "";
}

// Scratch memory, only while needed (about 14 KB).
struct Bufs { uint8_t table[kTableLen], ours[kTableLen], buf[kSector], chk[kSector]; };

void partitionReport(Print& out) {
  std::unique_ptr<Bufs> b(new (std::nothrow) Bufs);
  if (!b) { out.print("Out of memory.\r\n"); return; }
  uint8_t* table = b->table;
  uint8_t* ours = b->ours;
  if (esp_flash_read(nullptr, table, kTableAddr, kTableLen) != ESP_OK) { out.print("Can't read the partition table.\r\n"); return; }
  buildTable(ours);
  out.print("Partition table:\r\n");
  for (const uint8_t* p = table; p + 32 <= table + kTableLen && p[0] == 0xAA && p[1] == 0x50; p += 32) {
    char label[17] = {0};
    memcpy(label, p + 12, 16);
    out.printf("  %-9s type %02X/%02X  0x%06lX  %5lu KB\r\n", label, p[2], p[3],
               (unsigned long)le32(p + 4), (unsigned long)(le32(p + 8) / 1024));
  }
  const esp_partition_t* run = esp_ota_get_running_partition();
  if (run) out.printf("Running from %s at 0x%06lX; image %lu KB.\r\n", run->label, (unsigned long)run->address,
                      (unsigned long)(ESP.getSketchSize() / 1024));
  TableInfo ti = parseTable(table);
  uint32_t ra = 0, len = 0;
  String why = assess(ti, ra, len);
  if (why == "ok") out.printf("This is the firmware's own layout%s.\r\n",
                              memcmp(table, ours, kTableLen) == 0 ? " (identical to the built-in copy)" : " (same app slots)");
  else if (why.length()) out.printf("Foreign layout, and it can't be converted: %s.\r\n", why.c_str());
  else out.print("Foreign layout: it will be converted on the next boot.\r\n");
}

void migrateIfNeeded(Print& out) {
  std::unique_ptr<Bufs> b(new (std::nothrow) Bufs);
  if (!b) return;
  uint8_t *table = b->table, *ours = b->ours, *buf = b->buf, *chk = b->chk;
  if (esp_flash_read(nullptr, table, kTableAddr, kTableLen) != ESP_OK) return;
  TableInfo ti = parseTable(table);
  uint32_t runAddr = 0, imageLen = 0;
  String why = assess(ti, runAddr, imageLen);
  if (why == "ok") return;
  if (why.length()) { out.printf("Migrate: foreign partition table, not converting: %s.\r\n", why.c_str()); return; }

  out.printf("Migrate: foreign partition table (app slots 0x%lX + 0x%lX); converting to this firmware's layout.\r\n",
             (unsigned long)ti.app0Size, (unsigned long)ti.app1Size);
  carryOverWledWifi(out);

  if (runAddr != kApp0) {
    out.printf("Migrate: copying the firmware (%lu KB) from 0x%06lX to 0x%06lX...\r\n",
               (unsigned long)(imageLen / 1024), (unsigned long)runAddr, (unsigned long)kApp0);
    for (uint32_t off = 0; off < imageLen; off += kSector) {
      size_t n = imageLen - off < kSector ? imageLen - off : kSector;
      bool ok = esp_flash_read(nullptr, buf, runAddr + off, n) == ESP_OK &&
                eraseWrite(kApp0 + off, buf, n) == ESP_OK && flashEquals(kApp0 + off, buf, n, chk);
      if (!ok) { out.printf("Migrate: copy failed at 0x%06lX; staying on the old layout.\r\n", (unsigned long)(kApp0 + off)); return; }
    }
    esp_partition_pos_t pos = { kApp0, kLayout[2].size };
    esp_image_metadata_t md;
    if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &md) != ESP_OK || md.image_len != imageLen) {
      out.print("Migrate: the copy doesn't verify; staying on the old layout.\r\n");
      return;
    }
  }

  buildTable(ours);
  bool written = false;
  for (int tries = 0; tries < 3 && !written; tries++)
    written = eraseWrite(kTableAddr, ours, kTableLen) == ESP_OK && flashEquals(kTableAddr, ours, kTableLen, chk);
  if (!written) {
    out.print("Migrate: PARTITION TABLE WRITE FAILED. Restoring the old one.\r\n");
    for (int tries = 0; tries < 3; tries++)
      if (eraseWrite(kTableAddr, table, kTableLen) == ESP_OK && flashEquals(kTableAddr, table, kTableLen, chk)) return;
    out.print("Migrate: could not restore it either; the board needs a USB flash.\r\n");
    return;
  }
  // Empty otadata = boot app0, where the firmware now is.
  unprotected([](esp_flash_t* chip) { return esp_flash_erase_region(chip, 0xE000, 0x2000); });
  out.print("Migrate: done. Restarting on the new layout.\r\n");
  Serial.flush();
  delay(200);
  ESP.restart();
}
