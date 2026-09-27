#pragma once

// Attaches the servo and moves it to SERVO_REST_ANGLE.
void servo_init();

// Called repeatedly from the servo task. Drives the current movement
// pattern (rest / pet wiggle / alarm pattern) based on g_state.mood,
// without blocking (no delay() — use millis()-based timing internally).
void servo_tick();
