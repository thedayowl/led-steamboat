// ghupdate.cpp - check GitHub for a newer release and install it over Wi-Fi.
//
//  * The latest release's tag comes from the redirect of
//    https://github.com/<repo>/releases/latest  ->  .../releases/tag/v1.3.0
//    (no JSON to parse, and no API rate limit).
//  * The OTA image is .../releases/download/v1.3.0/led-steamboat-1.3.0-ota.bin,
//    which GitHub redirects to its asset host.
//  * Both connections verify the server certificate with the CA bundle built
//    into the Arduino core, so only GitHub can supply the firmware.
//  * The download runs in its own task, so the web page keeps answering and can
//    show progress. The LEDs stay dark while it runs (see main.cpp).
#include <WiFi.h>
#include "app.h"

#if ESP_ARDUINO_VERSION_MAJOR >= 3
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#define GH_SUPPORTED 1
#else
#define GH_SUPPORTED 0
#endif

static volatile GhState s_state = GH_IDLE;
static volatile int     s_pct = -1;          // download progress, -1 unknown
static char             s_latest[32] = "";   // latest release tag from the last check
static char             s_msg[128] = "";     // last result / error, for status displays
static char             s_tag[32] = "";      // tag being installed (read by the task)

GhState     ghState()  { return s_state; }
bool        ghBusy()   { return s_state == GH_DOWNLOADING || s_state == GH_DONE; }
int         ghPercent() { return s_pct; }
const char* ghLatest() { return s_latest; }
const char* ghMessage() { return s_msg; }

// "v1.3.0", "1.3.0", "1.2.0-18-gabc1234" -> {1,3,0}. False if not a version.
static bool parseVer(const char* s, int v[3]) {
  if (*s == 'v' || *s == 'V') s++;
  return sscanf(s, "%d.%d.%d", &v[0], &v[1], &v[2]) == 3;
}

// Is release tag newer than the running firmware? A build a few commits past
// v1.3.0 ("1.3.0-2-g...") counts as v1.3.0, so it is not offered v1.3.0 again.
bool ghNewer(const char* tag) {
  int a[3], b[3];
  if (!parseVer(tag, a)) return false;
  if (!parseVer(FW_VERSION, b) || strncmp(FW_VERSION, "0.0.0", 5) == 0) return true;   // "dev" or untagged build
  for (int i = 0; i < 3; i++) if (a[i] != b[i]) return a[i] > b[i];
  return false;
}

// Tags go into a URL; allow only plain characters.
static bool tagOk(const String& t) {
  if (t.length() == 0 || t.length() >= sizeof(s_tag)) return false;
  for (size_t i = 0; i < t.length(); i++) {
    char c = t[i];
    if (!isalnum((unsigned char)c) && c != '.' && c != '-' && c != '_') return false;
  }
  return true;
}

#if GH_SUPPORTED

bool ghCheck(Print& out) {
  if (ghBusy()) { out.print("An update is already in progress.\r\n"); return false; }
  if (WiFi.status() != WL_CONNECTED) { out.print("Updating from GitHub needs a connection to a home Wi-Fi network with internet access;\r\n"
                                                    "the controller isn't connected to one (access point only).\r\n"); return false; }
  s_state = GH_CHECKING;
  NetworkClientSecure client;
  client.useBuiltinCACertBundle();
  HTTPClient http;
  http.setTimeout(10000);
  http.setConnectTimeout(10000);
  http.setUserAgent("steamboat-lights/" FW_VERSION);
  String tag, err;
  if (!http.begin(client, "https://github.com/" GITHUB_REPO "/releases/latest")) {
    err = "could not start the request";
  } else {
    int code = http.GET();   // redirects are not followed: the Location header holds the tag
    String loc = http.getLocation();
    http.end();
    int p = loc.indexOf("/releases/tag/");
    if (code < 0) err = HTTPClient::errorToString(code);
    else if (p >= 0) tag = loc.substring(p + 14);
    else if (code == 302 || code == 301) err = "the repository has no releases yet";
    else err = String("unexpected reply, HTTP ") + code;
  }
  if (!err.length() && !tagOk(tag)) err = "unexpected release tag '" + tag + "'";
  if (err.length()) {
    snprintf(s_msg, sizeof(s_msg), "Check failed: %s", err.c_str());
    s_state = GH_FAILED;
    out.printf("%s\r\n", s_msg);
    return false;
  }
  strlcpy(s_latest, tag.c_str(), sizeof(s_latest));
  bool newer = ghNewer(s_latest);
  snprintf(s_msg, sizeof(s_msg), "Latest release %s; this is %s. %s", s_latest, FW_VERSION,
           newer ? "Update available." : "Up to date.");
  s_state = GH_IDLE;
  out.printf("%s\r\n", s_msg);
  if (newer) out.print("Type 'update install' to download and install it.\r\n");
  return true;
}

static void installTask(void*) {
  String tag = s_tag;
  String ver = (tag[0] == 'v' || tag[0] == 'V') ? tag.substring(1) : tag;
  String url = "https://github.com/" GITHUB_REPO "/releases/download/" + tag + "/led-steamboat-" + ver + "-ota.bin";
  {
    NetworkClientSecure client;
    client.useBuiltinCACertBundle();
    HTTPUpdate upd(20000);
    upd.rebootOnUpdate(false);                           // restart below, after the status is visible
    upd.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   // github.com -> asset host
    upd.onProgress([](int cur, int total) { s_pct = total > 0 ? (int)((int64_t)cur * 100 / total) : -1; });
    HTTPUpdateResult r = upd.update(client, url, FW_VERSION);
    if (r == HTTP_UPDATE_OK) {
      snprintf(s_msg, sizeof(s_msg), "Installed %s. Restarting.", tag.c_str());
      s_pct = 100;
      s_state = GH_DONE;
    } else {
      int e = upd.getLastError();
      String why = upd.getLastErrorString();
      if (e == HTTP_CODE_NOT_FOUND || why.indexOf("404") >= 0) why = "release " + tag + " has no led-steamboat-" + ver + "-ota.bin";
      snprintf(s_msg, sizeof(s_msg), "Update to %s failed: %s", tag.c_str(), why.c_str());
      s_state = GH_FAILED;
    }
  }
  if (s_state == GH_DONE) {
    delay(3000);   // let the web page and console show the result
    ESP.restart();
  }
  vTaskDelete(nullptr);
}

bool ghInstall(const String& tagIn, Print& out) {
  if (ghBusy() || s_state == GH_CHECKING) { out.print("An update is already in progress.\r\n"); return false; }
  if (Update.isRunning()) { out.print("A firmware upload is already in progress.\r\n"); return false; }
  String tag = tagIn;
  if (!tag.length()) {                       // latest release, only if newer
    if (!ghCheck(out)) return false;
    if (!ghNewer(s_latest)) {
      out.printf("Nothing to install. To reinstall it anyway: update install %s\r\n", s_latest);
      return true;
    }
    tag = s_latest;
  } else if (WiFi.status() != WL_CONNECTED) {
    out.print("Updating from GitHub needs a connection to a home Wi-Fi network with internet access;\r\n"
                                                    "the controller isn't connected to one (access point only).\r\n");
    return false;
  }
  if (tag[0] != 'v' && tag[0] != 'V' && isdigit((unsigned char)tag[0])) tag = "v" + tag;   // "1.3.0" -> "v1.3.0"
  if (!tagOk(tag)) { out.printf("'%s' is not a release tag (like v1.3.0).\r\n", tag.c_str()); return false; }
  strlcpy(s_tag, tag.c_str(), sizeof(s_tag));
  s_pct = 0;
  snprintf(s_msg, sizeof(s_msg), "Downloading %s from GitHub...", s_tag);
  s_state = GH_DOWNLOADING;
  // TLS needs a deep stack; run on the loop's core so Wi-Fi keeps core 0.
  if (xTaskCreatePinnedToCore(installTask, "ghupdate", 12288, nullptr, 1, nullptr, ARDUINO_RUNNING_CORE) != pdPASS) {
    snprintf(s_msg, sizeof(s_msg), "Could not start the update task.");
    s_state = GH_FAILED;
    out.printf("%s\r\n", s_msg);
    return false;
  }
  out.printf("%s The LEDs stay off; the controller restarts when it is done. 'update status' shows progress.\r\n", s_msg);
  return true;
}

#else   // Arduino core 2

bool ghCheck(Print& out) { out.print("Updating from GitHub needs a build with Arduino-ESP32 core 3.\r\n"); return false; }
bool ghInstall(const String&, Print& out) { return ghCheck(out); }

#endif

void ghStatus(Print& out) {
  switch (s_state) {
    case GH_DOWNLOADING:
      if (s_pct >= 0) out.printf("Downloading %s: %d%%\r\n", s_tag, s_pct);
      else out.printf("Downloading %s...\r\n", s_tag);
      break;
    case GH_CHECKING: out.print("Checking GitHub...\r\n"); break;
    default:
      if (s_msg[0]) out.printf("%s\r\n", s_msg);
      else out.printf("This is %s. Type 'update' to check GitHub (" GITHUB_REPO ") for a newer release.\r\n", FW_VERSION);
  }
}
