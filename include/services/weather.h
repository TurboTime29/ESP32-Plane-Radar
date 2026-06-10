#pragma once

#include <cstddef>

namespace services::weather {

/** Coarse condition class for picking an icon/color on the weather page. */
enum class Condition {
  Unknown,
  Clear,
  PartlyCloudy,
  Cloudy,
  Fog,
  Rain,
  Snow,
  Storm,
};

struct Data {
  bool valid = false;
  float temp_f = 0.0f;
  float feels_f = 0.0f;
  Condition condition = Condition::Unknown;
  char label[16] = "";  // e.g. "Sunny", "Rainy", "Cloudy"
};

/** Last fetched weather (valid == false until the first successful fetch). */
const Data& current();

/**
 * Fetch current weather from Open-Meteo for the given coordinates.
 * Returns true and updates current() on success. Requires Wi-Fi.
 */
bool fetch(double lat, double lon);

}  // namespace services::weather
