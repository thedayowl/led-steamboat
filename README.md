# Steamboat lights: ESP32 firmware

Standalone firmware for the stern-wheel steamboat display on a **GLEDOPTO GL-C-017WL-D** (ESP32, 4 outputs) driving 12 V WS2811 strings. Other ESP32 WLED controllers work too: pick the board from a preset list, or set the pins yourself (see [Board and pins](#board-and-pins)). This replaces WLED on the board. You can flash WLED back at any time from install.wled.me.

**What it does**
- Every part of the boat is a **section**. A section is a list of **runs** of LEDs. Runs can be anywhere on any of the strings (up to 8 outputs), in any order, and forwards or backwards.
- Static sections show a solid color.
- **Waterline**: shifting shades of blue that flow along the section in the order its runs are listed, regardless of how the string is routed. To see it without hardware, open the [waterline simulator](https://thedayowl.github.io/led-steamboat/waterline-simulator.html) ([source](docs/waterline-simulator.html)). It runs the firmware's formula live.
- **Smoke**: three puffs fade in one after another moving away from the stack, each fainter than the last, then a pause.
- **Paddle wheel**: three spoke sets light in rotation with a crossfade, so the wheel looks like it's turning. Both effects are in the [smoke and paddle wheel simulator](https://thedayowl.github.io/led-steamboat/smoke-wheel-simulator.html) ([source](docs/smoke-wheel-simulator.html)).
- **The whole boat**: the [display simulator](https://thedayowl.github.io/led-steamboat/display-simulator.html) ([source](docs/display-simulator.html)) shows the full display bulb by bulb, with the static sections, waterline, smoke and paddle wheel all running together.
- **Test tools** on the serial console and the web page, for finding LEDs and mapping sections without recompiling.
- **Photoresistor** on the GPIO13 connector (read on GPIO33) to turn the display off in daylight.
- **Board presets and runtime pins**: channel count, output GPIOs, relay, button and light sensor are set from the web page or console, with presets for common WLED controllers.
- **Backup and restore** of the whole configuration as an editable text file.
- **Firmware updates** straight from GitHub releases, or by upload from the web page or PlatformIO over Wi-Fi.
- Password-protected web page. Wi-Fi network, setup access point (WPA2) and admin password are all configurable at runtime.
- LED power relay and the Function button.

## Files

| File | What's in it |
|---|---|
| `include/config.h` | **Edit this.** First-boot defaults (pins, Wi-Fi/AP/admin), default section table, colors, timings, sensor |
| `include/types.h` | Section/role types |
| `src/main.cpp` | Startup, main loop, Wi-Fi/OTA, relay, button, light sensor |
| `src/hardware.cpp` | Channel count and pin settings, conflict checks, board presets |
| `src/display.cpp` | Sections and runs, effects, test patterns, saved settings |
| `src/console.cpp` | Serial console commands (the web page uses the same commands), backup |
| `src/web.cpp` | Web page, restore, firmware upload |
| `src/ghupdate.cpp` | Checks GitHub for a newer release and installs it over Wi-Fi |
| `src/migrate.cpp` | First boot after installing over WLED: converts the partition table, imports WLED's Wi-Fi |
| `src/led_driver.*` | Up to 8 channels of WS2811/WS2812B output on the ESP32 RMT peripheral (no external libraries) |
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
- `led-steamboat-<version>-ota.bin`: the app only, for boards already running this firmware (see [Firmware updates](#firmware-updates)), or for a WLED board without USB (below).

**From WLED over Wi-Fi (no USB):** a board running WLED can install this firmware through WLED's own update page:
1. In WLED, open **Config > Security & Updates > Manual OTA Update**. If **OTA lock** or a settings PIN is set, unlock it first.
2. Choose `led-steamboat-<version>-ota.bin`, tick **Ignore firmware validation** (WLED 0.15 and later refuse other firmware without it), and update.
3. On its first start the firmware converts WLED's partition table to its own and restarts. If the image landed in WLED's second slot, it first copies itself to the first. It also imports WLED's saved Wi-Fi network and password, so it comes back on your network at `http://steamboat.local`. Without a saved network it starts the setup access point.

This keeps WLED's bootloader, which works: tested with WLED 0.15.3 on IDF 3.3 and IDF 4.4 bootloaders, in both slots, followed by an update from GitHub. The serial console shows each step (`Migrate: ...`), and `partitions` shows the current layout. A power cut during the conversion is harmless except during the table write itself, which takes about 0.1 s; after that the board needs a USB flash. Going back to WLED: upload a WLED `.bin` on this firmware's **Firmware update** page (WLED then runs with this firmware's partition layout).

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

**LED outputs.** Terminal order on the board is GPIO16, GPIO12, GPIO4, GPIO2, which are channels 1, 2, 3, 4. Pins can be changed at runtime; see [Board and pins](#board-and-pins).

**Photoresistor (LDR)** on the board's GPIO13 connector, read on GPIO33. The connector has a signal pin and a ground lug but no 3.3 V, so the 10k pull-up goes inside the case and only two wires go out to the LDR.

GPIO13 can't be used to read the sensor: it is on the ESP32's ADC2, which can't take readings while Wi-Fi is on. So inside the case, a jumper joins the GPIO13 connector to GPIO33 (ADC1), and the firmware reads GPIO33. It keeps GPIO13 a floating input so it doesn't affect the reading (`SENSOR_JUMPER_PIN` in `config.h`).

```
           inside the case                │  outside (GPIO13 connector)
3.3V ──[ 10k ]──● GPIO33                  │
                │                         │
                └── jumper ──● GPIO13 ────┼──┐
                                          │ [ LDR ]
GND ──────────────────────────────────────┼──┘ (ground lug)
```

If GPIO33 isn't easy to reach, any ADC1 pin works (GPIO32-39; GPIO32 is the microphone on boards that have one): jumper to it and set `sensor pin 34` (or whichever).

More light lowers the LDR's resistance and pulls the pin lower. The sensor is set to inverted by default (`sensor invert on`), so the light level still reads higher in daylight. If you wire it the other way round (LDR to 3.3V, 10k to GND), use `sensor invert off`. Point the sensor at the sky and shield it from the display's own glow. If it can see the boat, the boat lighting up can make it think it's daytime.

**Relay** (GPIO18) switches LED power, active-low, matching Gledopto's WLED setup. If the strings get no power, try `relay low off`.

**Button** (GPIO17): a short press turns the display on/off. Hold it 5 s to start the setup access point.

12 V WS2811 strings still need power injection on long runs. Keep the data run from the board to the first pixel short.

## Board and pins

The channel count (1-8), each channel's GPIO, the relay, the button and the light sensor are settings, like everything else. Set them on the web page (**Board, channels and pins**) or in the console, then **save and restart**: outputs are only set up at boot. Until then `pins` and the web page show what will change. The defaults come from `config.h` and match the GL-C-017WL-D.

**Presets.** `preset` lists the boards below; `preset <name>` sets that board's channel count, output pins, relay and button. The light sensor is kept unless the board uses its pin. Pins come from the makers' pinout guides and manuals. Check them against your board's documentation before restarting, especially where the list says something isn't published.

| Preset | Board | LED outputs | Relay | Button |
|---|---|---|---|---|
| `generic` | Any ESP32 board, WLED's defaults | 16 | none | 0 |
| `gledopto017` | Gledopto GL-C-017WL-D | 16, 12, 4, 2 | 18 (active low) | 17 |
| `gledopto618` | Gledopto GL-C-618WL (Ethernet unused) | 16, 12, 4, 2 | 18 (active low) | 17 |
| `gledopto616` | Gledopto GL-C-616WL (Ethernet unused) | 16, 2 | 18 (active low) | 17 |
| `gledopto015` | Gledopto GL-C-015WL / 015WL-D | 16, 2 | 12 | not published |
| `dig2go` | QuinLED Dig2Go | 16 | 12 | 0 |
| `diguno3` | QuinLED Dig-Uno v3 | 16, 3 | 15 (Q1) | 0 |
| `digquad3` | QuinLED Dig-Quad v3 | 16, 3, 1, 4 | 15 (Q1R) | 0 |
| `diguno4` | QuinLED Dig-Uno v4 | 2, 4 | not published | 37 |
| `digquad4` | QuinLED Dig-Quad v4 | 2, 4, 12, 14 | not published | 37 |
| `digocta2` | QuinLED Dig-Octa, Brainboard-32-8L v2 | 0, 1, 2, 3, 4, 5, 12, 13 | 33 | 34 |
| `digocta4` | QuinLED Dig-Octa, Brainboard-32-8L v4 | 1, 2, 3, 4, 5, 12, 13, 14 | 33 (polarity not published) | 34 |
| `athommusic` | Athom ESP32 music controller | 18, 17 | 2 (polarity not published) | 0 |
| `athompd` | Athom PD energy-meter controller | 18, 5, 16, 17 | none | 0 |
| `athomdmx` | Athom Ethernet DMX controller V2 (Ethernet, DMX unused) | 5, 16, 4, 12 | 2 (polarity not published) | 0 |

Only original-ESP32 boards are listed; boards built on the ESP32-C3 or S3 need a different firmware build. If the strings get no power after picking a preset, the relay polarity is probably the other way: `relay low on` or `relay low off`.

**Pins by hand:**

| Command | Setting |
|---|---|
| `pins` | everything below, what's running now, and any problems |
| `channels <1-8>` | number of LED outputs (new ones start with 0 LEDs and no GPIO) |
| `chan <ch> pin <gpio\|none>` | output GPIO: 0-5, 12-19, 21-23, 25-27, 32, 33 |
| `relay pin <gpio\|none>`, `relay low on\|off` | LED power relay and its polarity (`on` = active low) |
| `button pin <gpio\|none>` | Function button (to GND; GPIO34-39 need an external pull-up) |
| `sensor pin <32-39\|none>`, `sensor invert on\|off` | light sensor (ADC1 only: ADC2 doesn't work with Wi-Fi on) |
| `pins default` | back to the `config.h` defaults |

A pin that can never work (flash pins 6-11, input-only pins for an output) is refused. A clash, such as two channels on one GPIO or an output on the relay pin, is accepted with a warning, so a restore that swaps pins doesn't fail halfway. At boot the clashing output is left off and `check` reports it. Sections on a channel number above the channel count are reported by `check` and not drawn. `defaults` doesn't touch pin settings. With more than 4 outputs, each gets one RMT memory block instead of two, and each channel in use takes 4.8 KB of RAM.

## Getting to the controls

- **Web page:** `http://steamboat.local`, or the IP shown by `info`. Log in with user name **admin** and the admin password (default `steamboat`; change it, see [Network settings](#network-settings)). If no home network is set, or it can't be joined within 20 s, the board starts its **setup access point**: join it (default name `Steamboat-Setup`, WPA2 password `steamboat`) and browse to `http://192.168.4.1`. Holding the Function button 5 s also starts the access point.
- **Serial console:** USB-C, 115200 baud (PlatformIO monitor, PuTTY, or Arduino Serial Monitor). Press **Enter**. The show keeps running until you start a test, and `run` goes back to the show. If there's no input for 15 minutes, it returns to the show by itself. No password is needed over USB.

LED numbers start at **0**. Channels and section numbers start at **1**. Sections can be named loosely: `roof`, `puff2`, `spoke07`, `wheelhousedoor`.

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

**Order matters for the waterline.** The water flows from the first LED of the first run to the last LED of the last run. So list the pieces in the order they sit along the boat, and write each piece in the direction the water should travel. Use first > last for any piece that's wired backwards. `rev <section>` flips the whole section if it ends up flowing the wrong way. For static, smoke and spoke sections the order doesn't matter. A section can have up to 16 runs; change `MAX_RUNS_PER_SECTION` in `config.h` if you need more.

## Mapping the strings (first-time setup)

The section table in `config.h` is generated from the planned string routing in [layout/routing/](layout/routing/README.md): channel 1 is panel 1, channel 2 is panel 2, channel 3 is panel 3, and output 4 is unused. If the strings are wired exactly as routed, check it with steps 1, 4 and 5 below. Otherwise, or if any count is off, map the real build like this.

1. **LED type and color order:** every channel defaults to WS2811. For other strings, such as WS2812B for testing, type `chan 1 type ws2812b` (or pick it in the web page's Channels table). That switches the data timing and sets the chip's usual color order. Types: `WS2811`, `WS2812B` (also WS2812, WS2813, WS2815 and SK6812 RGB), and `WS2811-400K` for older strings wired for 400 kHz. Then `rgb 1`. LED 0 should be red, LED 1 green, LED 2 blue. If not, try `chan 1 order GRB` (or BRG, RBG...) until it is. Repeat for channels 2–4.
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
5. **Animation order.** Puff 1 is the one nearest the stack. Each spoke is its own section (`Spoke 01`…`Spoke 16`), and its group (1, 2, 3, the sets A, B, C) sets the order the light travels. If the wheel turns backwards, type `wheeldir`. If the water flows the wrong way, type `rev waterline`.
6. **`save`.** Until you save, changes are live but lost on reboot. The prompt shows `*` when there are unsaved changes.
7. **Download a backup** (next section) once it's all mapped.

## Backup and restore

A backup is a plain text file of console commands covering:
- channel count, pins, relay, button and light sensor GPIOs
- channel LED counts, LED types and color orders
- every section's runs, color and direction
- show settings and light sensor settings
- the Wi-Fi and AP names

You can read and edit it.

- **Back up:** web page > **Backup & restore** > **Download backup**. Tick "include passwords" to also save the Wi-Fi, AP and admin passwords; leave it unticked for a copy that's safe to share. In the console, `backup` prints the same text, and `backup secrets` includes the passwords. From a PC: `curl -u admin:<password> http://steamboat.local/backup -o steamboat-backup.txt`.
- **Restore:** web page > **Backup & restore** > choose the file > **Restore**. Every line is applied, and any line that fails is listed. With "save after restoring" ticked, the result is saved to flash. You can also paste the file into the serial console, then type `save`.
- **Matching:** sections are matched by **name**, not position. A backup made before a firmware update that adds, removes or reorders sections still restores. Lines for sections that no longer exist are reported and skipped.
- Network and pin changes from a restore apply after a reboot.

## Firmware updates

Settings are kept across updates. They're stored separately from the firmware.

- **From GitHub:** web page > **Firmware update** > **Check GitHub for updates**. If a newer release exists, an **Install v1.x.x** button appears. It downloads the release's `-ota.bin` straight from GitHub, shows a progress bar, then restarts. The LEDs are dark while it installs. This needs the board connected to a home Wi-Fi network with internet access; in access-point-only mode the option is hidden. The connection to GitHub is HTTPS, with the certificate checked, so only this repository's releases can be installed. The console commands do the same: `update` checks, `update install` installs the latest release if it's newer, and `update install v1.2.0` installs a specific release, including older ones. The repository is set by `GITHUB_REPO` in `include/config.h`.
- **From the web page:** **Firmware update** > choose the `.bin` > **Upload firmware**. A progress bar shows the upload, then the board restarts and the page reconnects. Use the `-ota.bin` file from a release, or after a PlatformIO build use `.pio/build/usb/firmware.bin`. Don't upload `factory.bin` here. In Arduino IDE, use Sketch > Export Compiled Binary. A file that isn't an ESP32 firmware image is rejected without touching the running firmware.
- **From PlatformIO over Wi-Fi:** `pio run -e ota -t upload`. The `--auth=` value in `platformio.ini` must match the admin password.

If a new firmware version adds, removes or renames rows in the section table, it starts with the compiled defaults for sections. Download a backup before updating and restore it afterwards.

**Version 1.4.0 changes the partition layout.** The two firmware slots grew from 1.25 MB to 1.875 MB to make room for the HTTPS code. A board running 1.3.0 or older can't take 1.4.0 over Wi-Fi. Flash it once over USB with `pio run -e usb -t upload`; that writes the new partition table and keeps the settings. Updates over Wi-Fi work as before after that.

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
pins   preset [name]   channels <n>   chan <ch> pin <gpio|none>   pins default
relay pin <gpio|none>   relay low on|off   button pin <gpio|none>   sensor pin <32-39|none>   sensor invert on|off
ms <sec>   ma <sec>   me <sec>        start / add run / end run at the cursor
set <sec> <runs>   add <sec> <runs>   clear <sec>   color <sec> <RRGGBB>   rev <sec> [on|off]
chan <ch> count <n>   chan <ch> order <RGB|GRB|...>   chan <ch> type <WS2811|WS2812B|WS2811-400K>
bright <0-255>   speed <water|smoke|wheel> <%>   floor <0-255>   fade <0-255>   wheeldir [normal|reversed]
sensor [on|off|dark n|day n|delay s]   on   off
wifi [ssid <name> | pass <pw> | off]   ap ssid <name>   ap pass <pw>   admin pass <pw>
backup [secrets]   save   reload   defaults   info   partitions   reboot
update   update install [v1.x.x]   update status    firmware from GitHub releases
```

The web page has a console box that takes the same commands. You can also run single commands from a browser address bar, for example `http://steamboat.local/api/cmd?c=ends`; the browser asks for the admin login.

## GPIO1 note

GPIO1 is the ESP32's serial TX line, which goes to the USB-serial chip. The GL-C-017WL-D doesn't use it for an output, but some boards do (QuinLED Dig-Quad v3, Dig-Octa). When an LED channel is on GPIO1, the firmware shares the pin:

- During the normal show, the pin drives the LEDs.
- When the console is open, the pin switches back to serial. That channel's string may flicker random colors while text is sent.
- When you test that channel, the pin goes back to the LEDs and console output pauses. Typing still works, and the lit LED is your feedback.

Mapping that channel is easier from the web page. When a channel is on GPIO3 (serial RX), the serial console is turned off; use the web page.
