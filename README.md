# DeskClock

An ESP32 desk clock with a face. A 1.8" colour TFT shows a pair of expressive eyes over a live sky
(sun, moon, clouds, rain, wind), the time, and the weather. A small servo gives it a head, a sound
module gives it a voice, and three push buttons let you pet it, change pages and silence the alarm.
Everything else - alarm time, volume, city, timezone - is set from a small web page on your WiFi.

**Contents:** [Features](#features) · [Hardware](#hardware) · [Build and flash](#build-and-flash) ·
[First start](#first-start) · [Using it](#using-it) · [Settings page](#settings-page) ·
[Sound clips](#sound-clips) · [Configuration](#configuration) · [Project layout](#project-layout) ·
[Enclosure and 3D printing](#enclosure-and-3d-printing) · [Troubleshooting](#troubleshooting) ·
[Contributing](#contributing) · [License](#license)

## Features

- **Clock** with smooth digit animation, a breathing colon, an alarm indicator and a time-of-day colour theme
  (night / dawn / day / dusk) that follows the real sunrise and sunset.
- **Eyes with character**: blinking, looking around, winking, yawning, squinting, peek-a-boo, sleepy at night,
  worried when WiFi is gone, sweating when hot, shivering when cold, startled by lightning.
- **Idle life**: now and then he thinks ("hmm", sometimes an "oh!"), checks the time, chases a little fly with his eyes
  and head, and yawns in the evening or when you keep him up at night. See [Idle life](#idle-life).
- **Living background**: the sky behind the eyes follows the weather and the hour - sun and moon travelling
  across the sky, the real moon phase, drifting clouds, rain, snow, fog, lightning and stars. Clouds and
  rain follow the real wind speed and direction.
- **Pet button** with varied reactions (hearts, dizzy, annoyed, sleepy, raised eyebrow in rain, "missed you",
  morning yawn, rare winks and surprises) plus purring when held. Statistics are kept in flash.
- **Bond**: a lasting "how much does he like you" value that grows when you pet or purr him regularly and fades
  when you stay away. It makes him more patient, lets him smile by himself, tints his eyes, and when he is lonely
  he sighs now and then. See [Bond](#bond).
- **Yearly occasions**: Halloween, Valentine's Day, Christmas, Easter and New Year's Eve change the face on their own
  (bats, hearts, snow and a Santa hat, bunny ears, fireworks and a 10..1 countdown). See [Yearly occasions](#yearly-occasions).
- **Weather** from Open-Meteo (free, no API key): current conditions, today's high / low, feels-like, humidity,
  wind, UV, pressure, sunrise / sunset and hourly forecast bars with the chance of rain.
- **Date page** with the weekday, a big day number and a month calendar (today highlighted).
- **Alarm** with weekday mask, snooze, auto-off, missed-alarm catch-up and a servo sweep with red eyes.
  It keeps working without WiFi.
- **Night mode**: dimmed backlight, muted pet sound, sleepy eyes - from sunset to sunrise.
- **Web settings page** (Persian, right-to-left) with short feedback beeps, **OTA updates** (also straight from GitHub
  Releases, after you confirm, see [Updates from GitHub Releases](#updates-from-github-releases)), a diagnostics page on the
  screen, a task watchdog and a boot log that remembers why the chip reset.
- **Community enclosure**: the 3D-printed case is designed by the community. See
  [Enclosure and 3D printing](#enclosure-and-3d-printing).

## Hardware

| Part                                  | Notes                                                                           |
| ------------------------------------- | ------------------------------------------------------------------------------- |
| ESP32 dev board                       | `esp32dev`, 4 MB flash                                                          |
| 1.8" TFT, ST7735, 128x160             | SPI. Comes in several "tab" variants, see [Display variants](#display-variants) |
| JQ6500-16P MP3 module + small speaker | Audio clips live in the module's own flash                                      |
| Hobby servo (e.g. SG90)               | The "head"                                                                      |
| 3 x push buttons (microswitches)      | Pet, Mode, Alarm                                                                |

No extra parts are needed for anything described here.

### Wiring

Pins are defined in `include/pins.h`.

| Function          | GPIO      | Notes                                          |
| ----------------- | --------- | ---------------------------------------------- |
| TFT CS / DC / RST | 5 / 2 / 4 |                                                |
| TFT MOSI / SCLK   | 23 / 18   | default VSPI pins                              |
| TFT backlight     | 15        | dimmed at night                                |
| Pet button        | 32        | switch between the pin and GND                 |
| Mode button       | 33        | switch between the pin and GND                 |
| Alarm button      | 27        | switch between the pin and GND                 |
| Servo signal      | 13        |                                                |
| JQ6500 RX / TX    | 16 / 17   | ESP32 RX2 <- JQ6500 TX, ESP32 TX2 -> JQ6500 RX |

Buttons use `INPUT_PULLUP`: one leg to the GPIO, the other to GND. No resistors needed.

The servo and the speaker share a supply. If the screen flickers, the sound stutters or the web page shows
"Brownout" resets, use a better power supply or cable, and keep the servo off a weak USB port.

## Build and flash

The project uses [PlatformIO](https://platformio.org/) (VS Code extension or CLI).

```bash
pio run -e esp32dev -t upload     # build + flash over USB
pio device monitor                # serial log, 115200 baud
```

Libraries (installed automatically): TFT_eSPI, ESP32Servo, ArduinoJson 7, WiFiManager.

### Display variants

Cheap ST7735 modules differ in pixel offset and colour order. If the picture is shifted, has noisy lines on
two edges or wrong colours, try the next environment:

| Environment                            | Tab                       |
| -------------------------------------- | ------------------------- |
| `esp32dev` (default)                   | black tab                 |
| `esp32dev-green`, `-green2`, `-green3` | green tab and its offsets |
| `esp32dev-red`                         | red tab                   |

Extra flags for the environment that works best: `-D TFT_INVERSION_ON=1` (inverted colours),
`-D TFT_RGB_ORDER=0` (red/blue swapped), `-D SPI_FREQUENCY=10000000` (random pixels).

### Over-the-air updates

1. Flash once over USB.
2. After that: `pio run -e esp32dev-ota -t upload`.

Change `OTA_PASSWORD` in `include/config.h` **and** the `--auth=` value in `platformio.ini` (they must match).
The partition table (two 1.9 MB OTA slots, `partitions_ota_big.csv`) can only be changed over USB.

### Updates from GitHub Releases

Once a clock runs a firmware with this feature, it can update itself from the **Releases** of this repository,
with no cable and no computer. It is always your decision:

1. The clock looks for a new release **by itself, twice a day** (the first time a few minutes after it starts), and
   you can press **Check for updates** in _Firmware update_ on the settings page (`http://deskclock.local/`) at any
   time. Either way nothing is downloaded yet. The diagnostics page shows the running **Version** and whether an
   **Update** is ready, so you can see it on the clock itself.
2. If a newer release exists for your display type, the page shows its version, size and a link to the release notes.
3. Press **Install** and confirm. The clock downloads the file, checks it against the published SHA-256, writes it
   to the other OTA slot and reboots. The progress is shown on the clock's screen. It takes about a minute and
   the alarm cannot ring meanwhile. Do not unplug it. Your settings are kept.

Details:

- Only **official releases** are offered (not `main`, not pre-releases). The clock offers the file named
  `deskclock-<your display variant>.bin`, for example `deskclock-esp32dev-green.bin`. A build you made yourself
  reports its version as `dev`.
- The connection is TLS with certificate checking, so the clock needs the real time (NTP) first.
- A file whose checksum does not match is never activated. There is **no automatic rollback**, though: a release
  that crashes at boot has to be fixed over USB. Maintainers: see [docs/releasing.md](docs/releasing.md).
- Possible errors (no time yet, not enough memory, GitHub rate limit, no file for your variant, ...) are explained on the page.
- The automatic check only reports; it never installs. Change `UPDATE_AUTO_CHECK_INTERVAL_MS` (once a day: 24 h) or set
  `UPDATE_AUTO_CHECK 0` in `config.h` for manual checks only.
- Forks: set `UPDATE_REPO` in `config.h` to your own `owner/repo`. `UPDATE_ENABLED 0` removes the feature.
- The cable / `pio ... -ota` ways above keep working as before.

### Simulator and test environments

| Environment          | Purpose                                                             |
| -------------------- | ------------------------------------------------------------------- |
| `wokwi`              | Wokwi simulation (`diagram.json`, ILI9341 stands in for the ST7735) |
| `esp32dev-nighttest` | forces "always night" to test sleepy eyes and dimming               |
| `esp32dev-themetest` | replays a whole day of colours in under a minute                    |
| `esp32dev-wdttest`   | makes a task hang on purpose to prove the watchdog resets the chip  |

## First start

1. Power on. The clock opens a WiFi access point named **DeskClock**.
2. Join it from a phone, pick your network, enter its password.
3. The clock syncs the time (NTP) and fetches the weather.
4. Open **http://deskclock.local/** from a device on the same WiFi to set the alarm, city and timezone.

To forget the saved WiFi network: **hold the Mode button while powering on**.

Until the first NTP sync the clock restores the last time saved in flash (shown as approximate), so the alarm
still works after a power cut without WiFi.

## Using it

### Buttons

| Button    | Tap                                                                        | Hold                                                                |
| --------- | -------------------------------------------------------------------------- | ------------------------------------------------------------------- |
| **Pet**   | A reaction (not while the alarm rings)                                     | after 1 s: purring (stops when released, or after 8 s)              |
| **Mode**  | Clock -> Date -> Weather -> Clock                                          | 1 s: open / close the diagnostics page (a ring fills while holding) |
| **Alarm** | While ringing: snooze (5 min). After 3 snoozes the next tap ends the alarm | 1 s: switch the alarm off, also while snoozed                       |

The Date, Weather and diagnostics pages return to the clock by themselves after 15 s without a button press.
An unattended alarm stops by itself after 3 minutes. The next day's alarm is never affected by dismissing one.

### Pages

- **Clock**: the face, the time, the alarm bell with its time (red and flashing while ringing).
- **Date**: weekday, big day number, month and year, and a month calendar.
- **Weather**: two views that alternate every few seconds. _Now_: icon, big temperature, condition, today's
  high / low, feels-like, humidity, wind (speed and arrow) and chance of rain. _Forecast_: hourly bars with the
  temperature and the chance of rain, plus sunrise, sunset, UV and pressure. Old readings are shown dimmed with
  their age.
- **Diagnostics**: IP address, WiFi signal, free memory, last NTP sync, age of the weather reading, firmware version and
  whether a newer release is available.

### Idle life

While nothing else is happening (daytime, no reaction showing) he does a small thing by himself every 1-2.5 minutes
(the `MICRO_*` rhythm): peek-a-boo, an eye roll, a squint in bright sun, and these:

| Behaviour | What you see                                                                                                                                                                               |
| --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Thinking  | looks up and to the right, three dots bounce; half the time an "oh!" at the end                                                                                                            |
| Glance    | looks down at the digits and they glow once, as if checking the time                                                                                                                       |
| Fly       | a small fly enters on one side, loops around for about 6.5 s and leaves on the other; the eyes follow it and the head turns a little                                                       |
| Yawn      | in the last 90 minutes before night mode starts, a yawn every 12-26 minutes; at night, if you wake him and keep him up, one yawn 25 s after the touch                                      |
| Sneeze    | rare: "ah ... ah ... choo!" - eyes squint and look up twice, then the burst (eyes shut, a kick, a small spray, the head snaps aside) and a sheepish glance; likelier in rain, snow or cold |

If the head turns the wrong way for the fly, set `SERVO_LOOK_DIR` to `-1` in `config.h`; `IDLE_FLY_HEAD 0` keeps the head
still. Every behaviour has an `IDLE_*_ENABLED` switch. The sneeze is a rare pick (`IDLE_SNEEZE_PERCENT`, plus `IDLE_SNEEZE_WEATHER_BONUS` in
rain / snow / cold), `IDLE_SNEEZE_HEAD 0` keeps the head still, and `SOUND_INDEX_SNEEZE` plays a clip of your own (0 = silent).

### Pet reactions

In priority order: fast repeated taps give hearts, then dizzy, then annoyed; the first tap of the morning gives a
yawn; the first tap after a long absence gives "missed you"; a rare random one gives a wink or a surprise; then
night (sleepy), rain (raised eyebrow), heat (sweating), cold (shivering); otherwise the classic happy face.
All of it can be turned off on the settings page (then it is always the happy face).

### Bond

The clock remembers how much it likes you (0-100 %, kept in flash, shown on the settings page under _Pet reactions_).

- **Grows** with taps (+1 % each, not while dizzy-spamming), with purring (+1.5 % per hold) and with making up after a
  sulk (+2 %). Taps and purring together add at most 12 % per day, so one long session cannot max it out.
  "That's enough!" costs 3 %.
- **Fades** when nobody pets him: nothing for 12 hours after the last tap, then 8 % per day, never below 5 %.
  The fading is only counted with a clock that was NTP-synced (not one restored from flash).
- **Levels**: _lonely_ (under 25 %), _friendly_ (the start, 40 %), _fond_ (55 %) and _devoted_ (80 %).

| Level    | What changes                                                                                                                                                        |
| -------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Lonely   | slightly washed-out eyes; every 20-50 min of an idle day he sighs; sulks last 1.5x as long                                                                          |
| Friendly | the classic behaviour                                                                                                                                               |
| Fond     | +2 taps before dizzy / "that's enough!", more rare reactions, faint pink eyes, smiles by himself, sulks 25 % shorter                                                |
| Devoted  | +4 taps before dizzy / "that's enough!", heart eyes one tap sooner, even more rare reactions, pinker eyes, smiles or gets heart eyes by himself, sulks half as long |

Nothing about it happens at night or while the alarm is involved. It follows the _varied reactions_ switch: off means
the bond neither changes nor shows. All numbers are `PET_BOND_*` in `config.h` (`PET_BOND_ENABLED 0` removes the feature,
`PET_BOND_TINT 0` only the eye colour).

### Yearly occasions

On a few dates the face changes by itself. Everything is drawn on the screen, no extra files or hardware are needed.

| Occasion        | When (defaults)                                                        | What you see                                                         |
| --------------- | ---------------------------------------------------------------------- | -------------------------------------------------------------------- |
| Halloween       | Oct 28 - 31                                                            | bats flying past, a glowing jack-o'-lantern, orange eyes             |
| Valentine's Day | Feb 13 - 14                                                            | hearts drifting up, blushing cheeks, a beating heart, pink eyes      |
| Christmas       | Dec 20 - 26                                                            | falling snow, a Santa hat, a string of blinking lights               |
| Easter          | Good Friday to Easter Monday (Western Easter, computed for every year) | bunny ears (one twitches now and then), pastel eggs that hop         |
| New Year's Eve  | Dec 31 from 18:00 until Jan 1 03:00                                    | fireworks, confetti, a party hat, a 10..1 countdown and the new year |

The countdown starts at 23:59:50 and the new year appears at midnight, so the clock needs a valid time (NTP or the time restored from flash).
The dates are the `OCC_*` values in `config.h`. The whole feature can be switched off on the settings page
(`OCCASIONS_DEFAULT 0` changes the factory default), and the settings page can preview each occasion for 25 s at any time of year.

## Settings page

`http://deskclock.local/` (or the IP shown on the diagnostics page). Values saved here are kept in flash and
override the defaults in `config.h`.

| Section         | What you can set                                                            |
| --------------- | --------------------------------------------------------------------------- |
| Alarm           | on / off, time, weekdays, sound, alarm volume, preview                      |
| Pet sound       | volume of the touch sound, preview, feedback beeps for settings changes     |
| Occasions       | yearly occasion animations on / off, preview of each occasion (25 s)        |
| Pet reactions   | varied reactions on / off, pet statistics, bond level                       |
| Alarm test      | ring the alarm now, snooze it, dismiss it                                   |
| City            | name and coordinates used for weather and for sunrise / sunset              |
| Timezone        | a list of common zones or a custom POSIX rule (DST is handled)              |
| Firmware update | check GitHub for a new release and install it after you confirm             |
| Diagnostics     | boot log with the reason for each reset (power on, brownout, watchdog, ...) |

To protect the page, set `WEB_UI_PASSWORD` in `config.h` (HTTP Basic auth, user `admin`).

## Sound clips

The JQ6500 plays clips from its own flash by **upload order** (1 = the first file copied to the module).
Copy all clips in numeric order. Suggested layout:

| #          | Clip                                                  | Config                                                                    |
| ---------- | ----------------------------------------------------- | ------------------------------------------------------------------------- |
| 1, 2, 4, 5 | alarm tones (2 is the default and plays on repeat)    | `SOUND_INDEX_ALARM` = 2                                                   |
| 3          | pet touch chirp                                       | `SOUND_INDEX_PET` = 3                                                     |
| 6, 7       | two more touch chirps (so it never repeats)           | `SOUND_INDEX_PET_B`, `SOUND_INDEX_PET_C`                                  |
| 8          | yawn                                                  | `SOUND_INDEX_YAWN`                                                        |
| 9          | hourly chime (off by default)                         | `SOUND_INDEX_CHIME` = 0, set it to 9 to enable                            |
| 10         | wake-up blip                                          | `SOUND_INDEX_BOOT`                                                        |
| 11, 12, 14 | settings feedback: switched on / switched off / error | `SOUND_INDEX_UI_ON`, `_UI_OFF`, `_UI_ERROR` (switch: `UI_SOUNDS_DEFAULT`) |
| 13         | tick                                                  | `SOUND_INDEX_TICK`, not used by the firmware yet                          |

`AUDIO_FILE_COUNT` (14) is the number of clips on the module. Anything set to `0` stays silent. Touch sounds, yawns and chimes are muted at night
(`AUDIO_NIGHT_PET_VOLUME`); the alarm is never muted. Use mono MP3 at a modest bit rate and keep the
clips of one family at a similar loudness, since they share one volume setting.

## Configuration

Almost everything is a `#define` in `include/config.h`, each with a comment. The most useful ones:

| Setting                                                   | What it does                                                                                      |
| --------------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| `WEB_HOSTNAME`, `WEB_UI_PASSWORD`, `OTA_PASSWORD`         | network name and passwords                                                                        |
| `DEFAULT_TZ_*`, `WEATHER_*`                               | factory timezone, city and refresh interval                                                       |
| `ALARM_*`                                                 | factory alarm, snooze length, snooze limit, auto-off, grace period                                |
| `NIGHT_FOLLOWS_SUN`, `NIGHT_START_HOUR`, `NIGHT_END_HOUR` | when night mode starts and ends                                                                   |
| `BACKLIGHT_*`                                             | how the screen is dimmed (drive strength or PWM)                                                  |
| `PET_*`                                                   | streak lengths, purr time, rare-reaction chance                                                   |
| `PET_BOND_*`                                              | how fast the bond grows and fades, its levels, tint and self-started acts                         |
| `IDLE_*`, `SERVO_LOOK_*`                                  | thinking, glance, fly, yawn and sneeze behaviours, how far the head follows the fly               |
| `SERVO_*`                                                 | rest / pet / alarm angles and timings                                                             |
| `OCCASIONS_DEFAULT`, `OCC_*`                              | yearly occasion animations: default on / off and their dates                                      |
| `UI_SOUNDS_DEFAULT`                                       | feedback beeps when settings are changed                                                          |
| `UPDATE_ENABLED`, `UPDATE_REPO`, `UPDATE_MIN_FREE_HEAP`   | firmware update from GitHub Releases: on / off, which repository, memory needed                   |
| `UPDATE_AUTO_CHECK`, `UPDATE_AUTO_CHECK_INTERVAL_MS`      | automatic check for a new release (default twice a day); installing still needs your confirmation |
| `SCENE_*`, `AMBIENT_FX`, `WEATHER_*_ANIM`                 | the background and the animations - set to 0 to switch off                                        |
| `TASK_WDT_*`                                              | watchdog timeout                                                                                  |

## Project layout

```
include/        config.h (all tunables), pins.h, state.h, settings.h, occasion.h, version.h, headers,
                web_page.h (the settings page), update_certs.h (generated root certificates)
src/main.cpp    FreeRTOS tasks: display, touch, servo, alarm, time, weather, web, OTA
src/touch.cpp   buttons: debounce, tap / hold logic, auto-return to the clock
src/pet.cpp     which reaction a tap gives, and the statistics kept in flash
src/alarm_ctrl.cpp, audio.cpp, servo_ctrl.cpp
src/time_sync.cpp, weather.cpp, sun.cpp, settings.cpp, web_ui.cpp, ota.cpp, boot_log.cpp
src/update_check.cpp  firmware update from GitHub Releases (check, confirm, download, SHA-256, install)
src/display_*.cpp   the screen, split by job:
                    core (canvases, frame loop), eyes / face / emotion (expressions),
                    persona (boot, sleep, chime, sulking), scene / ambient (background),
                    occasion (yearly face animations, dates in occasionUpdate()),
                    theme / backlight, digits / fonts / widgets, pages (date, weather),
                    info / screens / diag
tools/          make_vlw.py builds the smooth fonts (needs Pillow); vlw_check.py checks them;
                make_update_certs.py regenerates include/update_certs.h
.github/        Issue / PR templates; workflows: Build (every PR) and Release firmware (on a v* tag)
docs/           hardware measurements, mechanical design brief, assembly notes
3d-models/      community enclosure designs (CAD, STL), see its README
```

Tasks and what they share: the display, touch, servo and alarm tasks run on core 1 and are watched by the
watchdog; time, weather, web and OTA run on core 0. `g_state` is the small shared state guarded by a mutex.

## Enclosure and 3D printing

DeskClock is open to community enclosure designs. Designers, makers and engineers are welcome.

- [`3d-models/README.md`](3d-models/README.md): what is needed, deliverables and folder conventions.
- [`docs/hardware-measurements.md`](docs/hardware-measurements.md): measurement sheet for every module. Please fill it in
  for your own parts; boards and displays sold under the same name differ between suppliers.
- [`docs/mechanical-design.md`](docs/mechanical-design.md): the mechanical design brief and requirements.
- [`docs/assembly.md`](docs/assembly.md): assembly notes, to be completed by the enclosure contributor.
- To propose or share a design, open an Issue with the **3D Design Proposal** template, then send a Pull Request.
  See [CONTRIBUTING.md](CONTRIBUTING.md).

There is no official enclosure yet. A design counts as tested only after it has been printed and fitted to the real hardware.

## Troubleshooting

| Symptom                                                              | Try                                                                                                                                |
| -------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| Picture shifted, noisy edges, wrong colours                          | another tab environment (see above), then the extra flags                                                                          |
| Random coloured pixels                                               | `-D SPI_FREQUENCY=10000000`, shorter wires, power supply                                                                           |
| `deskclock.local` not found                                          | use the IP from the diagnostics page; Windows may need Bonjour                                                                     |
| Resets with "Brownout" in the boot log                               | stronger supply / cable; servo and speaker share it                                                                                |
| Wrong sound plays                                                    | the files were copied in a different order; check the numbers in `config.h`                                                        |
| No time after a power cut                                            | the clock restores the saved time and fixes it as soon as WiFi is back                                                             |
| Wrong WiFi network                                                   | hold Mode while powering on to open the setup portal again                                                                         |
| "Check for updates" says no time yet                                 | wait for the first NTP sync (diagnostics page shows the last sync)                                                                 |
| "Check for updates" says secure connection failed                    | GitHub may have changed its certificate authority: `python tools/make_update_certs.py`, see [docs/releasing.md](docs/releasing.md) |
| Upload fails ("Failed to connect") or no boot with the display wired | GPIO2 (TFT DC) and GPIO15 (backlight) are ESP32 strapping pins: unplug those two wires, flash / reset, then plug them back         |

## Planned

Making the three buttons more useful so that common settings (alarm on/off and time, volume) work without
the web page, with short on-screen confirmations.

## Contributing

Firmware developers, electronics enthusiasts, CAD designers and 3D-printing makers are welcome.
Read [CONTRIBUTING.md](CONTRIBUTING.md), check the open Issues, and for bigger changes open an Issue first.
Never commit WiFi passwords, OTA passwords or other secrets.

## License

- Firmware, software and tools: [MIT License](LICENSE).
- 3D models and CAD files in [`3d-models/`](3d-models/): [CC BY-SA 4.0](3d-models/LICENSE.md).
