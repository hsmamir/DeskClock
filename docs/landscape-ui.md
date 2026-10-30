# Landscape UI

The display is used in landscape (160 x 128). One full-screen page at a time, drawn like the clock character:
navy background, white "neon" strokes with a blue glow, pink blush accents.

| Page                  | How you get there                                                                                             |
| --------------------- | ------------------------------------------------------------------------------------------------------------- |
| Face (the eyes, home) | default; turn the knob: Face -> Clock -> Date -> Weather -> Face (counter-clockwise goes back)                |
| Clock                 | turn the knob; style = quick menu -> **Clock** (push to cycle)                                                |
| Date                  | turn the knob; weekday, big day number, month, year and a month calendar (week start = `CALENDAR_WEEK_START`) |
| Weather               | turn the knob; icon, temperature, condition, feels like / humidity / wind, the next hours with rain chance    |
| Quick menu            | push the knob                                                                                                 |
| Diagnostics           | hold the knob                                                                                                 |
| Reminder              | appears when one is due                                                                                       |

Clock styles: Simple, Analog, Minimal, Retro, LED (`src/display_clockstyles.cpp`). The chosen style is saved in NVS (`ui_state.cpp`).

Eyes: round white eyes with a dark iris and two highlights, lilac blush, a tiny mouth and neon brows, drawn procedurally (no bitmaps)
from the design sheet: happy, excited (star eyes), wink, sleepy, surprised, love, sad, angry, thinking, suspicious, worried, hot, cold, confused
(`display_face.cpp`, `display_character.cpp`). Plain navy behind them; `-D LANDSCAPE_SCENE=1` brings the weather scene back.

Build flags (platformio.ini `build_flags`):

- `-D LANDSCAPE_UI=0` the old portrait layout (two stacked cards)
- `-D DISPLAY_ROTATION=3` panel mounted the other way round (default 1)
- `-D LANDSCAPE_SCENE=1` weather / time-of-day scene behind the eyes
- `-D UI_STYLE=0`, `-D CHARACTER_STYLE=0` the old flat widgets / the old LED eyes

Date and Weather are drawn in `src/display_page_side.cpp` with the fonts of `include/display_fonts_pages.h` (regenerate them with
`python3 tools/make_vlw_pages.py`). They return to the home view after `DISPLAY_RETURN_MS` without input, a tap on the knob goes home at once.
The old scrolling Weather page (7-day forecast, air quality) is only in the portrait layout (`-D LANDSCAPE_UI=0`).

Credits easter egg: tap the touch pad `EGG_CREDITS_TAPS` times quickly while the diagnostics page is open (a tap during the credits skips them).
