#include "services/touch.h"

#include <Arduino.h>
#include <Wire.h>

#include "config.h"

namespace services::touch {

namespace {

bool s_ready = false;
bool s_in_touch = false;           // finger currently down (level state)
unsigned long s_last_tap_ms = 0;   // for the blocked-window fallback guard

constexpr int kIntPin = static_cast<int>(config::kTouchPinInt);
constexpr unsigned long kQuietGapMs = 300;

// Set by the INT-pin interrupt. Used only as a fallback: it catches a tap that
// starts AND ends while the main loop is blocked in an HTTP fetch, where the
// I2C polling below would otherwise never see the finger.
volatile bool s_touch_irq = false;

void IRAM_ATTR onTouchIsr() { s_touch_irq = true; }

bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(config::kTouchI2cAddr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readReg(uint8_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(config::kTouchI2cAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const size_t got = Wire.requestFrom(
      static_cast<int>(config::kTouchI2cAddr), static_cast<int>(len));
  if (got != len) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    buf[i] = Wire.read();
  }
  return true;
}

}  // namespace

void init() {
  // Hardware reset pulse (CST816 needs ~50 ms to boot afterwards).
  pinMode(static_cast<uint8_t>(config::kTouchPinRst), OUTPUT);
  digitalWrite(static_cast<uint8_t>(config::kTouchPinRst), LOW);
  delay(10);
  digitalWrite(static_cast<uint8_t>(config::kTouchPinRst), HIGH);
  delay(60);

  Wire.begin(static_cast<int>(config::kTouchPinSda),
             static_cast<int>(config::kTouchPinScl), 400000);
  // Disable auto-sleep so the controller keeps ACKing the bus when idle, so the
  // level polling below works without I2C errors (0xFE = 1).
  writeReg(0xFE, 0x01);

  pinMode(kIntPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(kIntPin), onTouchIsr, FALLING);

  s_ready = true;
}

bool tapped() {
  if (!s_ready) {
    return false;
  }

  // Level state: number of fingers currently touching (reg 0x02 = 0 or 1).
  uint8_t fingers = 0;
  const bool ok = readReg(0x02, &fingers, 1);
  const bool down = ok && fingers > 0;

  const bool irq = s_touch_irq;
  s_touch_irq = false;
  const unsigned long now = millis();

  bool tap = false;
  if (down) {
    if (!s_in_touch) {  // rising edge of a press -> one tap
      s_in_touch = true;
      tap = true;
    }
  } else {
    if (s_in_touch) {
      s_in_touch = false;  // finger lifted; session over
    } else if (irq && now - s_last_tap_ms >= kQuietGapMs) {
      // A whole tap happened between two polls (loop was blocked); the INT
      // interrupt caught it even though the finger is already up.
      tap = true;
    }
  }

  if (tap) {
    s_last_tap_ms = now;
  }
  return tap;
}

}  // namespace services::touch
