#pragma once

// Attaches the servo and moves it to SERVO_REST_ANGLE.
void servo_init();

// Called repeatedly from the servo task. Drives the current movement
// pattern (rest / pet wiggle / alarm pattern) based on g_state.mood,
// without blocking (no delay() — use millis()-based timing internally).
void servo_tick();

// The head follows the eyes for a while (the fly): x = -1..1 is how far to turn from the rest angle (SERVO_LOOK_DEG at
// most). Only while the servo is idle - a pet reaction or the alarm takes over at once. Call every frame while it lasts;
// servo_look_end() lets the head drift back to rest. Safe from any task.
void servo_look(float x);
void servo_look_end();