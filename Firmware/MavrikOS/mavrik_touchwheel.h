// Mavrik OS — iPod-style touch wheel: three copper pads in a ring, read with the
// ESP32-S3's built-in touch sensing. Sliding a finger around the ring gives wheel steps.
#pragma once
#include <Arduino.h>
#include "mavrik_config.h"

// Pads sit at 90°, 210° and 330° around the ring (A at the top, then clockwise).
static const uint8_t TW_PINS[3] = {PIN_ENC_CLK, PIN_ENC_DT, PIN_TOUCH_C};
static const float TW_ANGLE[3] = {90.0f, -30.0f, 210.0f};   // degrees, counter-clockwise from +x
static float twBase[3];
static bool twDown = false;
static float twLastDeg = 0, twAccum = 0;

void touchWheelBegin() {
  for (int p = 0; p < 3; p++) {          // resting level, with no finger on the wheel
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) { sum += touchRead(TW_PINS[p]); delay(2); }
    twBase[p] = sum / 16.0f;
  }
}

// Call often. Returns whole steps since the last call: + is clockwise.
int touchWheelPoll() {
  float w[3], total = 0;
  for (int p = 0; p < 3; p++) {
    float raw = touchRead(TW_PINS[p]);
    float d = (raw - twBase[p]) / twBase[p];   // S3 readings rise when touched
    w[p] = d > TOUCH_THRESHOLD ? d : 0;
    total += w[p];
    if (d < TOUCH_THRESHOLD * 0.3f) twBase[p] += (raw - twBase[p]) * 0.002f;   // follow slow drift
  }
  if (total <= 0) { twDown = false; return 0; }
  float x = 0, y = 0;
  for (int p = 0; p < 3; p++) {
    float a = TW_ANGLE[p] * DEG_TO_RAD;
    x += w[p] * cosf(a); y += w[p] * sinf(a);
  }
  float deg = atan2f(y, x) * RAD_TO_DEG;
  if (!twDown) { twDown = true; twLastDeg = deg; twAccum = 0; return 0; }
  float d = deg - twLastDeg;
  if (d > 180) d -= 360; else if (d < -180) d += 360;
  twLastDeg = deg;
  twAccum += -d;                          // finger moving clockwise lowers the angle
  int steps = (int)(twAccum / TOUCH_STEP_DEG);
  twAccum -= steps * TOUCH_STEP_DEG;
  return steps;
}
