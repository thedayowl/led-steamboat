# Steamboat lights: ESP32 firmware

Standalone firmware for the stern-wheel steamboat display on a **GLEDOPTO GL-C-017WL-D** (ESP32, 4 outputs) driving 12 V WS2811 strings. This replaces WLED on the board. You can flash WLED back at any time from install.wled.me.

**What it does**
- Every part of the boat is a **section**. A section is a list of **runs** of LEDs. Runs can be anywhere on any of the 4 strings, in any order, and forwards or backwards.
- Static sections show a solid color.
- **Waterline**: shifting shades of blue that flow along the section in the order its runs are listed, regardless of how the string is routed. To see it without hardware, open [docs/waterline-simulator.html](docs/waterline-simulator.html) in a browser. It runs the firmware's formula live.
- **Smoke**: three puffs fade in one after another moving away from the stack, each fainter than the last, then a pause.
- **Paddle wheel**: three spoke sets light in rotation with a crossfade, so the wheel looks like it's turning.
- **Test tools** on the serial console and the web page, for finding LEDs and mapping sections without recompiling.
- **Photoresistor** on GPIO33 to turn the display off in daylight.
- **Backup and restore** of the whole configuration as an editable text file.
- **Firmware updates** from the web page or PlatformIO over Wi-Fi.
- Password-protected web page. Wi-Fi network, setup access point (WPA2) and admin password are all configurable at runtime.
- LED power relay and the Function button.

## Files

| File | What's in it |
|---|---|
| `include/config.h` | **Edit this.** GPIOs, first-boot Wi-Fi/AP/admin defaults, default section table, colors, timings, sensor |
| `include/types.h` | Section/role types |
| `src/main.cpp` | Startup, main loop, Wi-Fi/OTA, relay, button, light sensor |
| `src/display.cpp` | Sections and runs, effects, test patterns, saved settings |
| `src/console.cpp` | Serial console commands (the web page uses the same commands), backup |
| `src/web.cpp` | Web page, restore, firmware upload |
| `src/led_driver.*` | 4-channel WS2811 output on the ESP32 RMT peripheral (no external libraries) |
| `scripts/version.py` | Build step: sets the firmware version from the git tag |
| `scripts/factory.py` | Build step: writes `factory.bin`, the full flash image |

## Build and flash

**PlatformIO (VS Code)**
1. Open the `led-steamboat` folder.
2. Optionally set first-boot defaults in `include/config.h`: `WIFI_SSID` / `WIFI_PASSWORD`, `AP_SSID` / `AP_PASSWORD`, `ADMIN_PASSWORD`. You can also leave them and set everything from the web page later.
3. Plug the board in with USB-C and run **Upload** for the `usb` environment (`pio run -e usb -t upload`).
4. For later updates, see [Firmware updates](#firmware-updates).

**Arduino IDE (alternative):** create a folder `steamboat` containing an empty `steamboat.ino`. Copy every file from `src/` and `include/` into that folder, select board "ESP32 Dev Module", and upload. Use Arduino-ESP32 **core 3.3.12**, the version PlatformIO builds with. Arduino IDE builds report their version as `dev`.

**About the platform:** the firmware uses Arduino-ESP32 core 3.x, which is built on ESP-IDF 5. The official PlatformIO platform stops at core 2.0.17, so `platformio.ini` uses the community [pioarduino](https://github.com/pioarduino/platform-espressif32) platform. pioarduino installs its own fork of PlatformIO and its own toolchains, so `platformio.ini` gives it a separate folder (`~/.platformio-pioarduino`). Other projects in `~/.platformio` aren't affected. The first build downloads several hundred MB.

**Prebuilt firmware:** each [GitHub release](https://github.com/thedayowl/led-steamboat/releases) has two files:
- `led-steamboat-<version>-factory.bin`: the full image (bootloader, partition table and app) for a new board or one running WLED. Flash it at offset 0x0, for example `esptool.py --chip esp32 write_flash 0x0 led-steamboat-1.2.0-factory.bin` or with the [ESP web flasher](https://espressif.github.io/esptool-js/).
- `led-steamboat-<version>-ota.bin`: the app only, for boards already running this firmware. See [Firmware updates](#firmware-updates).

## Versions and releases

The firmware version comes from git when PlatformIO builds it. It shows on the serial console at startup, in `info`, on the web page and in backup files.

| Build from | Version |
|---|---|
| The commit tagged `v1.2.0` | `1.2.0` |
| 3 commits after that tag | `1.2.0-3-gabc1234` (`abc1234` is the commit) |
| Any build with uncommitted changes | ends in `-dirty` |

So a board running a clean release shows a plain number, and anything else tells you exactly which commit it came from.

To make a release, commit and push your changes, then tag the commit and push the tag:

```
git tag v1.3.0
git push origin v1.3.0
```

GitHub Actions ([.github/workflows/build.yml](.github/workflows/build.yml)) then builds the firmware and creates the release with both `.bin` files attached. It also builds every push to `main` and every pull request. The two files from those builds are under **Artifacts** on each run in the Actions tab.

A local `pio run -e usb` produces the same two files: `.pio/build/usb/firmware.bin` (the OTA image) and `.pio/build/usb/factory.bin` (the full image).

## Wiring

**LED outputs.** Terminal order on the board is GPIO16, GPIO12, GPIO4, GPIO2, which are channels 1, 2, 3, 4. To change pins, edit `LED_PINS` in `config.h`.

**Photoresistor (LDR)** on GPIO33 as a voltage divider:

```
3.3V ──[ LDR ]──┬── GPIO33
                └──[ 10k ]── GND
```

More light gives a higher reading. If you wire it the other way round, set `LIGHT_SENSOR_INVERT true`. Point the sensor at the sky and shield it from the display's own glow. If it can see the boat, the boat lighting up can make it think it's daytime.

**Relay** (GPIO18) switches LED power, active-low, matching Gledopto's WLED setup. If the strings get no power, flip `RELAY_ACTIVE_LOW`.

**Button** (GPIO17): a short press turns the display on/off. Hold it 5 s to start the setup access point.

12 V WS2811 strings still need power injection on long runs. Keep the data run from the board to the first pixel short.

## Getting to the controls

- **Web page:** `http://steamboat.local`, or the IP shown by `info`. Log in with user name **admin** and the admin password (default `steamboat`; change it, see [Network settings](#network-settings)). If no home network is set, or it can't be joined within 20 s, the board starts its **setup access point**: join it (default name `Steamboat-Setup`, WPA2 password `steamboat`) and browse to `http://192.168.4.1`. Holding the Function button 5 s also starts the access point.
- **Serial console:** USB-C, 115200 baud (PlatformIO monitor, PuTTY, or Arduino Serial Monitor). Press **Enter**. The show keeps running until you start a test, and `run` goes back to the show. If there's no input for 15 minutes, it returns to the show by itself. No password is needed over USB.

LED numbers start at **0**. Channels and section numbers start at **1**. Sections can be named loosely: `roof`, `puff2`, `spokesa`, `wheelhousedoor`.

## Network settings

Set these on the web page (**Network** section) or in the console. Then `save` and reboot.

| Command | Setting |
|---|---|
| `wifi` | show current network settings (no passwords shown) |
| `wifi ssid <name>` / `wifi pass <password>` | home network (`wifi pass none` for an open network) |
| `wifi off` | no home network; run the setup access point only |
| `ap ssid <name>` | setup access point name |
| `ap pass <password>` | setup access point **WPA2** password, 8–63 characters. Anything shorter is rejected, so the AP never runs open. |
| `admin pass <password>` | web page login (user `admin`) and OTA upload password |

The values in `config.h` are only used on first boot, or until you save your own. `defaults` resets the show and sections but leaves network settings alone.

## How sections are written

A section is a comma-separated list of runs: `channel:first-last`.

| Example | Meaning |
|---|---|
| `1:0-89` | channel 1, LEDs 0 to 89 |
| `2:17` | a single LED |
| `1:0-40, 1:120-160` | two separate pieces of channel 1 |
| `1:200-170` | first > last: the run is **backwards**, 200, 199 … 170 |
| `1:0-10, 40-50, 90-80` | the channel can be left off to reuse the previous one |
| `1:90-120, 1:200-170, 2:5-9` | pieces on two strings |

**Order matters for the waterline.** The water flows from the first LED of the first run to the last LED of the last run. So list the pieces in the order they sit along the boat, and write each piece in the direction the water should travel. Use first > last for any piece that's wired backwards. `rev <section>` flips the whole section if it ends up flowing the wrong way. For static, smoke and spoke sections the order doesn't matter. A section can have up to 12 runs; change `MAX_RUNS_PER_SECTION` in `config.h` if you need more.

## Mapping the strings (first-time setup)

The section table in `config.h` uses placeholder LED numbers. Map the real build like this.

1. **Color order:** `rgb 1`. LED 0 should be red, LED 1 green, LED 2 blue. If not, try `chan 1 order GRB` (or BRG, RBG...) until it is. Repeat for channels 2–4.
2. **LED count per channel:** `ruler 1` lights LED 0 green, every 5th blue, every 10th red and every 100th magenta, so you can count along the string. Then `chan 1 count 287` (your number). `fill 1` lights the whole channel to confirm the count.
3. **Mark each section with the cursor.** Move the cursor with `p 1 0`, `+`/`-` (±1), `]`/`[` (±10) or the arrow keys. Every move prints which section that LED is currently in. `walk 1` steps along automatically, and any key stops it.
   - `ms hull`: start the section over, with its first run beginning at the cursor.
   - Move to the end of that piece, then `me hull` to end the run there. If the end is a lower number than the start, the run is recorded as backwards.
   - For each further piece, move to its start and type `ma hull` (add run), then move to its end and type `me hull`.
   - On the web page, use the **Start**, **Add run** and **End** buttons on the section's row. You can also type the runs straight into its text box and press Apply.
   - In the console, `set waterline 1:90-120, 1:200-170` replaces a section's runs, `add` appends runs, and `clear` empties it.
4. **Check:**
   - `s waterline` sends a white dot through the section in animation order, with each run's start green and end red. This is the way to confirm the water direction and the run order.
   - `ends` shows the start and end of every run of every section.
   - `check` reports overlaps, LEDs used twice, runs past the end of a channel, and LEDs not in any section.
5. **Animation order.** Puff 1 is the one nearest the stack. Spokes A→B→C is the direction the light travels. If the wheel turns backwards, type `wheeldir`. If the water flows the wrong way, type `rev waterline`.
6. **`save`.** Until you save, changes are live but lost on reboot. The prompt shows `*` when there are unsaved changes.
7. **Download a backup** (next section) once it's all mapped.

## Backup and restore

A backup is a plain text file of console commands covering:
- channel LED counts and color orders
- every section's runs, color and direction
- show settings and light sensor settings
- the Wi-Fi and AP names

You can read and edit it.

- **Back up:** web page > **Backup & restore** > **Download backup**. Tick "include passwords" to also save the Wi-Fi, AP and admin passwords; leave it unticked for a copy that's safe to share. In the console, `backup` prints the same text, and `backup secrets` includes the passwords. From a PC: `curl -u admin:<password> http://steamboat.local/backup -o steamboat-backup.txt`.
- **Restore:** web page > **Backup & restore** > choose the file > **Restore**. Every line is applied, and any line that fails is listed. With "save after restoring" ticked, the result is saved to flash. You can also paste the file into the serial console, then type `save`.
- **Matching:** sections are matched by **name**, not position. A backup made before a firmware update that adds, removes or reorders sections still restores. Lines for sections that no longer exist are reported and skipped.
- Network changes from a restore apply after a reboot.

## Firmware updates

Settings are kept across updates. They're stored separately from the firmware.

- **From the web page:** **Firmware update** > choose the `.bin` > **Upload firmware**. A progress bar shows the upload, then the board restarts and the page reconnects. Use the `-ota.bin` file from a release, or after a PlatformIO build use `.pio/build/usb/firmware.bin`. Don't upload `factory.bin` here. In Arduino IDE, use Sketch > Export Compiled Binary. A file that isn't an ESP32 firmware image is rejected without touching the running firmware.
- **From PlatformIO over Wi-Fi:** `pio run -e ota -t upload`. The `--auth=` value in `platformio.ini` must match the admin password.

If a new firmware version adds, removes or renames rows in the section table, it starts with the compiled defaults for sections. Download a backup before updating and restore it afterwards.

## Daylight auto-off

1. Run `sensor` (or watch the web page) at dusk to see the light level as a percentage.
2. `sensor dark 20`: the display comes on when the light drops below 20%.
3. `sensor day 35`: the display goes off when the light rises above 35%. Keep a gap between the two values.
4. `sensor delay 60`: the level must stay past a threshold for 60 s before switching.
5. `sensor on`, then `save`.

The button and `on`/`off` are a master switch. When the display is on, the sensor decides whether it's lit. Test patterns always show.

## Command reference

Type `help` in the console for the full list. The main ones:

```
p <ch> <led>, + - ] [ arrows   cursor          walk <ch> [ms]   auto-step
r <ch> <a> <b>   ruler <ch>   fill <ch> [RRGGBB]   rgb <ch>   s <section>   ends   blank   run
list   chans   check
ms <sec>   ma <sec>   me <sec>        start / add run / end run at the cursor
set <sec> <runs>   add <sec> <runs>   clear <sec>   color <sec> <RRGGBB>   rev <sec> [on|off]
chan <ch> count <n>   chan <ch> order <RGB|GRB|...>
bright <0-255>   speed <water|smoke|wheel> <%>   floor <0-255>   fade <0-255>   wheeldir [normal|reversed]
sensor [on|off|dark n|day n|delay s]   on   off
wifi [ssid <name> | pass <pw> | off]   ap ssid <name>   ap pass <pw>   admin pass <pw>
backup [secrets]   save   reload   defaults   info   reboot
```

The web page has a console box that takes the same commands. You can also run single commands from a browser address bar, for example `http://steamboat.local/api/cmd?c=ends`; the browser asks for the admin login.

## GPIO1 note

GPIO1 is the ESP32's serial TX line, which goes to the USB-serial chip. The GL-C-017WL-D doesn't use it for an output. If you set an LED channel to GPIO1 anyway, the firmware shares the pin:

- During the normal show, the pin drives the LEDs.
- When the console is open, the pin switches back to serial. That channel's string may flicker random colors while text is sent.
- When you test that channel, the pin goes back to the LEDs and console output pauses. Typing still works, and the lit LED is your feedback.

Mapping that channel is easier from the web page. GPIO3 (serial RX) can't be an LED pin if you want the serial console.
