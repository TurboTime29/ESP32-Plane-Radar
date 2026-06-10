#include "hardware/display.h"

#include "hardware/display_font.h"

LGFX tft;

void displayInit() {
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(40);  // ~16% — chosen level: cooler, low power, easy on the eyes
  tft.setTextWrap(false);
  displayFontInit();
}
