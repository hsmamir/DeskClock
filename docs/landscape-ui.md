# Landscape UI

The display is used in landscape (160 x 128). One full-screen page at a time, drawn like the clock character:
navy background, white "neon" strokes with a blue glow, pink blush accents.

| Page | How you get there |
|------|-------------------|
| Face (the eyes, home) | default; turn the knob to switch between face and clock |
| Clock | turn the knob; style = quick menu -> **Clock** (push to cycle) |
| Quick menu | push the knob |
| Diagnostics | hold the knob |
| Reminder | appears when one is due |

Clock styles: Simple, Analog, Minimal, Retro, LED (`src/display_clockstyles.cpp`). The chosen style is saved in NVS (`ui_state.cpp`).

Eyes: round white eyes with a dark iris and two highlights, lilac blush, a tiny mouth and neon brows, drawn procedurally (no bitmaps)
from the design sheet: happy, excited (star eyes), wink, sleepy, surprised, love, sad, angry, thinking, suspicious, worried, hot, cold, confused
(`display_face.cpp`, `display_character.cpp`). Plain navy behind them; `-D LANDSCAPE_SCENE=1` brings the weather scene back.

Build flags (platformio.ini `build_flags`):

* `-D LANDSCAPE_UI=0`    the old portrait layout (two stacked cards)
* `-D DISPLAY_ROTATION=3` panel mounted the other way round (default 1)
* `-D LANDSCAPE_SCENE=1`  weather / time-of-day scene behind the eyes
* `-D UI_STYLE=0`, `-D CHARACTER_STYLE=0` the old flat widgets / the old LED eyes

Not in the landscape UI yet: the Date and Weather pages (not reachable until they are redesigned), the credits easter egg.
