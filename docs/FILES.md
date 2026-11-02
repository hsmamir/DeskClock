# File map

One line per file, so you (or an AI assistant) can tell which files a change touches without reading the whole project.
**Send / read only the files named under "Where to look" for the feature at hand.**

The firmware has two UI layouts, chosen by `LANDSCAPE_UI` in `config.h`. **1 (default) = the landscape UI, 160 x 128.**
Files marked *(portrait)* are only used by `LANDSCAPE_UI=0` and can be ignored for normal work.

## Where to look

| Change                                           | Files                                                                  |
| ------------------------------------------------ | ---------------------------------------------------------------------- |
| Knob / touch pad behaviour, page order           | `touch.cpp`, `touch.h`, `state.h`, `display.cpp`                       |
| Control shade (the pull-down menu) and its tiles | `quick_menu.*`, `display_menu.cpp`, `touch.cpp`, `display_landscape.h` |
| First-boot setup / factory reset                 | `setup_wizard.*`, `display_setup.cpp`, `settings.*`, `touch.cpp`       |
| Alarm                                            | `alarm_ctrl.*`, `settings.*`, `audio.*`, `quick_menu.cpp`              |
| A new setting (saved in flash)                   | `settings.h`, `settings.cpp`, `web_ui.cpp`, `web_page.h`               |
| Clock faces                                      | `display_clockstyles.cpp`, `display_digits.cpp`, `ui_state.*`          |
| Date / Weather pages                             | `display_page_side.cpp`, `weather.cpp`, `weather_extras.h`             |
| Eyes / expressions / reactions                   | `display_eyes.cpp`, `display_face.cpp`, `display_persona.cpp`, `pet.*` |
| Sounds                                           | `audio.*`, `config.h` (`SOUND_INDEX_*`)                                |
| Look of the pages (colours, neon bars, icons)    | `display_style.*`, `display_internal.h`, `display_theme.cpp`           |
| Constants and tunables                           | `config.h`, `pins.h`, `platformio.ini`                                 |

## Tasks (`main.cpp`)
`display` (display_tick) | `touch` (touch_tick: pad, knob, quick menu, setup wizard) | `servo` | `alarm` (alarm_tick, audio_tick,
reminders_tick) | `time` (NTP) | `weather` | `web` (settings page) | `ota`.
They share `g_state` (`state.h`) under `g_stateMutex`. Anything slow (flash write, audio command) must not run while holding it.

## include/

| File                                                                        | What it is                                                                              |
| --------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| `config.h`                                                                  | every tunable: WiFi, sounds, alarm, timings, backlight, `LANDSCAPE_UI`                  |
| `pins.h`                                                                    | GPIO pins (also mirrored in `platformio.ini` for TFT_eSPI)                              |
| `state.h`                                                                   | `AppState` (`g_state`), `Mood`, `DisplayMode` (CLOCK, DATE, WEATHER, DIAG, MENU, SETUP) |
| `settings.h`                                                                | `Settings` struct, NVS load / save, factory reset, "setup done" flag                    |
| `ui_state.h`                                                                | clock style and home view (face or clock), saved across reboots                         |
| `quick_menu.h`                                                              | control shade model: tiles (`MenuItem`), `MenuView`, factory-reset confirm              |
| `setup_wizard.h`                                                            | first-boot setup steps, `SetupView`, place list                                         |
| `touch.h`                                                                   | input overview: touch pad and knob                                                      |
| `alarm_ctrl.h`                                                              | alarm scheduler API (snooze, dismiss, state)                                            |
| `audio.h`                                                                   | JQ6500 sound module API (play, loop, preview, gentle)                                   |
| `servo_ctrl.h`                                                              | servo API                                                                               |
| `pet.h`                                                                     | the pet's personality and affection                                                     |
| `reminders.h`                                                               | stored reminders and the on-screen reminder state                                       |
| `occasion.h`, `jalali.h`                                                    | yearly occasions, Persian calendar                                                      |
| `weather.h`, `weather_extras.h`, `weather_mood.h`                           | weather fetch, forecast / air quality data, weather-driven moods                        |
| `sun.h`                                                                     | sunrise / sunset, night window, moon phase                                              |
| `time_sync.h`                                                               | NTP, saved time across reboots, WiFi portal                                             |
| `web_ui.h`, `web_page.h`                                                    | settings web page (`web_page.h` is the embedded HTML)                                   |
| `ota.h`, `update_check.h`, `update_certs.h`, `version.h`                    | wireless update, update check, firmware version                                         |
| `boot_log.h`, `tls_lock.h`                                                  | boot counter / reset reasons, memory guard for TLS                                      |
| `display.h`                                                                 | `display_init()` and `display_tick()`                                                   |
| `display_fonts*.h`, `display_vazir.h`, `persian_forms.h`, `character_art.h` | **generated** fonts and artwork, never edit                                             |
| `persian_text.h`                                                            | Persian / Arabic text for a plain left-to-right renderer                                |

## src/ (logic)

| File                              | What it is                                                                        |
| --------------------------------- | --------------------------------------------------------------------------------- |
| `main.cpp`                        | setup(), task creation, watchdog, stack / heap report                             |
| `touch.cpp`                       | touch pad and knob: pages, taps, holds, hold ring, auto-return to the clock       |
| `quick_menu.cpp`                  | control shade logic (cursor, editing, saving, System page, factory-reset confirm) |
| `setup_wizard.cpp`                | first-boot setup logic                                                            |
| `settings.cpp`                    | settings in NVS, timezone, factory reset                                          |
| `ui_state.cpp`                    | clock style / home view storage                                                   |
| `alarm_ctrl.cpp`                  | alarm scheduling, ringing, snooze, "already rang today" marker                    |
| `audio.cpp`                       | JQ6500 commands, volume, night muting, rate limits                                |
| `servo_ctrl.cpp`                  | servo movement per mood                                                           |
| `pet.cpp`                         | pet mood and affection, saved in NVS                                              |
| `reminders.cpp`                   | reminders                                                                         |
| `time_sync.cpp`                   | NTP, WiFi connect and setup portal                                                |
| `weather.cpp`, `weather_mood.cpp` | Open-Meteo fetch, forecast, air quality, hot / cold / wind moods                  |
| `sun.cpp`                         | sun and moon maths                                                                |
| `web_ui.cpp`                      | settings web server and API                                                       |
| `ota.cpp`, `update_check.cpp`     | OTA and update check                                                              |
| `boot_log.cpp`, `tls_lock.cpp`    | boot log, TLS memory guard                                                        |

## src/ (display, landscape UI)

| File                                                                 | What it is                                                                              |
| -------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| `display.cpp`                                                        | `display_tick()`: decides which page is drawn this frame                                |
| `display_internal.h`                                                 | shared declarations of all display files (namespace `disp`): palette, canvases, helpers |
| `display_landscape.h`                                                | the landscape pages' draw functions                                                     |
| `display_core.cpp`                                                   | TFT object, layout, colour helpers, canvases, state readers                             |
| `display_style.*`                                                    | the neon look: top bar, divider, bars, sparkle, selection frame                         |
| `display_theme.cpp`                                                  | background / accent colours follow the sun                                              |
| `display_menu.cpp`                                                   | control shade drawing: slide-down, tiles, System and confirm pages                      |
| `display_setup.cpp`                                                  | first-boot setup pages                                                                  |
| `display_clockstyles.cpp`                                            | the five clock faces (Simple, Analog, Minimal, Retro, LED)                              |
| `display_digits.cpp`                                                 | seven-segment digits, colon, digit slide animation                                      |
| `display_page_side.cpp`                                              | Date and Weather pages                                                                  |
| `display_diag.cpp`                                                   | diagnostics page                                                                        |
| `display_reminder.cpp`                                               | full-screen reminder                                                                    |
| `display_screens.cpp`                                                | WiFi portal and OTA screens, WiFi-lost icon, hold ring                                  |
| `display_credits.cpp`                                                | hidden credits (easter egg)                                                             |
| `display_widgets.cpp`                                                | shared widgets: alarm badge, big temperature, wind arrow                                |
| `display_backlight.cpp`                                              | backlight dimming at night                                                              |
| `display_face.cpp`, `display_eyes.cpp`, `display_character.*`        | the character's face and eyes                                                           |
| `display_emotion.cpp`                                                | emotional eye colour                                                                    |
| `display_persona.cpp`                                                | longer behaviours: wake-up, sulking, yawns, hiccups, fly ...                            |
| `display_moments.cpp`, `display_occasion.cpp`, `display_ambient.cpp` | special minutes, yearly occasions, ambient effects                                      |
| `display_scene.cpp`, `display_wxicon.*`                              | weather glyphs, face background scene, animated weather icons                           |
| `persian_text.cpp`                                                   | Persian text shaping                                                                    |

## src/ (portrait layout, `LANDSCAPE_UI=0` only)
`display_info.cpp` (info panel), `display_pages.cpp` (page flow and slide), `display_page_date.cpp`, `display_page_weather.cpp`,
`display_wx.cpp` (+ `include/display_wx.h`).

## Generated files (never edit, never send to an AI)
`display_fonts.cpp`, `display_fonts_pages.cpp`, `display_vazir.cpp` (1.2 MB), `include/character_art.h`, `include/display_fonts*.h`,
`include/display_vazir.h`, `include/persian_forms.h`, `include/update_certs.h`. Regenerate them with the scripts in `tools/`.

## Other
`docs/landscape-ui.md` (UI pages and controls), `docs/assembly.md`, `docs/hardware-measurements.md`, `docs/mechanical-design.md`,
`docs/releasing.md`, `diagram.json` and `wokwi.toml` (Wokwi simulator), `partitions_ota_big.csv`, `tools/`, `3d-models/`.
