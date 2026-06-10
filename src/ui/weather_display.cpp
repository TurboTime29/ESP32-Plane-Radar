#include "ui/weather_display.h"

#include <WiFi.h>
#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/weather.h"
#include "ui/radar_theme.h"

namespace fonts = lgfx::v1::fonts;

namespace ui {

namespace {

using services::weather::Condition;

constexpr int kCx = config::kDisplayWidth / 2;

/** This GC9A01 board swaps R/B (see initPalette in radar_display): feed colors
 *  through here so logical RGB renders correctly. */
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  if (config::kDisplayRgbOrder) {
    return tft.color565(b, g, r);
  }
  return tft.color565(r, g, b);
}

// Match the radar background exactly: it uses the unswapped color path, so the
// same constants render as the same near-black tone (no bluish wash).
uint16_t bgColor() {
  return tft.color565(radar::kBgR, radar::kBgG, radar::kBgB);
}
uint16_t textColor() { return rgb(255, 255, 255); }

// ---- weather icons (centered on cx, cy) ----

void drawSun(int cx, int cy, int r, uint16_t color) {
  for (int a = 0; a < 360; a += 45) {
    const float rad = a * 0.01745329252f;
    const int x1 = cx + static_cast<int>(std::cos(rad) * (r + 4));
    const int y1 = cy + static_cast<int>(std::sin(rad) * (r + 4));
    const int x2 = cx + static_cast<int>(std::cos(rad) * (r + 11));
    const int y2 = cy + static_cast<int>(std::sin(rad) * (r + 11));
    tft.drawWideLine(x1, y1, x2, y2, 1.5f, color);
  }
  tft.fillCircle(cx, cy, r, color);
}

void drawCloud(int cx, int cy, uint16_t color) {
  tft.fillCircle(cx - 16, cy + 2, 12, color);
  tft.fillCircle(cx + 16, cy + 2, 13, color);
  tft.fillCircle(cx - 3, cy - 9, 15, color);
  tft.fillCircle(cx + 9, cy - 3, 13, color);
  tft.fillRect(cx - 27, cy + 2, 54, 13, color);
}

void drawRaindrops(int cx, int cy, uint16_t color) {
  for (int i = -1; i <= 1; ++i) {
    const int x = cx + i * 14;
    tft.drawWideLine(x, cy, x - 4, cy + 12, 1.5f, color);
  }
}

void drawSnowflakes(int cx, int cy, uint16_t color) {
  for (int i = -1; i <= 1; ++i) {
    tft.fillCircle(cx + i * 14, cy + 6, 3, color);
  }
}

void drawBolt(int cx, int cy, uint16_t color) {
  tft.fillTriangle(cx - 5, cy, cx + 6, cy, cx - 2, cy + 14, color);
  tft.fillTriangle(cx + 3, cy + 8, cx + 11, cy + 8, cx - 3, cy + 26, color);
}

void drawIcon(Condition cond, int cx, int cy) {
  const uint16_t sun = rgb(255, 200, 0);
  const uint16_t cloud = rgb(205, 210, 220);
  const uint16_t cloud_dark = rgb(120, 132, 150);
  const uint16_t rain = rgb(70, 150, 255);
  const uint16_t snow = rgb(240, 245, 255);
  const uint16_t bolt = rgb(255, 215, 0);
  const uint16_t fog = rgb(180, 190, 200);

  switch (cond) {
    case Condition::Clear:
      drawSun(cx, cy, 20, sun);
      break;
    case Condition::PartlyCloudy:
      drawSun(cx + 12, cy - 12, 12, sun);
      drawCloud(cx - 2, cy + 6, cloud);
      break;
    case Condition::Cloudy:
      drawCloud(cx, cy + 2, cloud);
      break;
    case Condition::Fog:
      drawCloud(cx, cy - 6, fog);
      for (int i = 0; i < 3; ++i) {
        tft.drawWideLine(cx - 22, cy + 16 + i * 7, cx + 22, cy + 16 + i * 7,
                         1.5f, fog);
      }
      break;
    case Condition::Rain:
      drawCloud(cx, cy - 6, cloud_dark);
      drawRaindrops(cx, cy + 14, rain);
      break;
    case Condition::Snow:
      drawCloud(cx, cy - 6, cloud);
      drawSnowflakes(cx, cy + 12, snow);
      break;
    case Condition::Storm:
      drawCloud(cx, cy - 6, cloud_dark);
      drawBolt(cx, cy + 12, bolt);
      break;
    case Condition::Unknown:
    default:
      tft.drawCircle(cx, cy, 18, fog);
      break;
  }
}

/** Draw "[prefix ]NN°F" centered on (cx, y). Degree is a small ring because the
 *  bundled fonts are ASCII-only (no ° glyph). */
void drawTempLine(int cx, int y, float value_f, const lgfx::GFXfont* font,
                  uint16_t color, const char* prefix) {
  displayFontSetBitmap(tft, font);
  tft.setTextColor(color, bgColor());

  char num[8];
  std::snprintf(num, sizeof(num), "%d",
                static_cast<int>(std::lround(value_f)));

  const int h = tft.fontHeight();
  const int ring_r = std::max(2, h / 9);
  constexpr int kGapNumDeg = 2;
  constexpr int kGapDegF = 1;
  constexpr int kGapPrefix = 6;

  const bool has_prefix = (prefix != nullptr && prefix[0] != '\0');
  const int prefix_w = has_prefix ? tft.textWidth(prefix) + kGapPrefix : 0;
  const int num_w = tft.textWidth(num);
  const int f_w = tft.textWidth("F");
  const int deg_w = kGapNumDeg + ring_r * 2 + kGapDegF;
  const int total = prefix_w + num_w + deg_w + f_w;

  tft.setTextDatum(textdatum_t::top_left);
  const int top = y - h / 2;
  int x = cx - total / 2;

  if (has_prefix) {
    tft.drawString(prefix, x, top);
    x += prefix_w;
  }
  tft.drawString(num, x, top);
  x += num_w + kGapNumDeg;

  const int ring_cx = x + ring_r;
  const int ring_cy = top + ring_r + 1;
  tft.drawCircle(ring_cx, ring_cy, ring_r, color);
  if (ring_r >= 3) {
    tft.drawCircle(ring_cx, ring_cy, ring_r - 1, color);  // thicker stroke
  }
  x += ring_r * 2 + kGapDegF;

  tft.drawString("F", x, top);
}

}  // namespace

void weatherDisplayDraw() {
  displayFontEnsureLoaded(tft);
  const uint16_t bg = bgColor();
  tft.fillScreen(bg);

  const services::weather::Data& w = services::weather::current();

  if (!w.valid) {
    displayFontSetBitmap(tft, &fonts::FreeSansBold12pt7b);
    tft.setTextColor(textColor(), bg);
    tft.setTextDatum(textdatum_t::middle_center);
    tft.drawString("Weather", kCx, 100);
    displayFontSetBitmap(tft, &fonts::FreeSans9pt7b);
    tft.drawString(WiFi.status() == WL_CONNECTED ? "Loading..." : "No Wi-Fi",
                   kCx, 134);
    tft.setTextDatum(textdatum_t::top_left);
    return;
  }

  drawIcon(w.condition, kCx, 70);

  displayFontSetBitmap(tft, &fonts::FreeSansBold12pt7b);
  tft.setTextColor(textColor(), bg);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.drawString(w.label, kCx, 126);

  drawTempLine(kCx, 162, w.temp_f, &fonts::FreeSansBold18pt7b, textColor(),
               nullptr);
  drawTempLine(kCx, 198, w.feels_f, &fonts::FreeSans9pt7b, rgb(180, 190, 205),
               "Feels");

  tft.setTextDatum(textdatum_t::top_left);
}

}  // namespace ui
