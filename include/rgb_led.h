#pragma once

// 4-pin RGB LED (common anode: one shared + pin, one - pin per colour), see "RGB LED" in config.h and pins.h.
// It shows what the clock is doing even when you are not looking at the screen:
//   alarm ringing ........ red, fast flashing                 (always, also at night)
//   firmware update ...... purple breathing                   (always)
//   WiFi setup portal .... blue breathing                     (always)
//   reminder on screen ... cyan double blink                  (not at night)
//   pet reaction ......... pink glow (purring = slow breathing; annoyed = orange, dizzy = violet,
//                          hot = warm orange, cold = ice blue)  (not at night)
//   otherwise ............ off, or a faint steady colour (RGB_LED_IDLE_MODE)  (off at night)

// Sets the three pins to "LED off" and attaches PWM. Call at the very start of setup().
void rgb_led_init();

// Call about every 20 ms from its own task (see main.cpp).
void rgb_led_tick();
