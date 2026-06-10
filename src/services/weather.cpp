#include "services/weather.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cstring>

namespace services::weather {

namespace {

Data s_data;

void setLabel(char* out, const char* text) {
  strncpy(out, text, sizeof(Data::label) - 1);
  out[sizeof(Data::label) - 1] = '\0';
}

/** Map WMO weather interpretation codes (Open-Meteo) to a class + label. */
void classify(int code, Condition* cond, char* label) {
  if (code == 0) {
    *cond = Condition::Clear;
    setLabel(label, "Sunny");
  } else if (code == 1 || code == 2) {
    *cond = Condition::PartlyCloudy;
    setLabel(label, "Partly Cloudy");
  } else if (code == 3) {
    *cond = Condition::Cloudy;
    setLabel(label, "Cloudy");
  } else if (code == 45 || code == 48) {
    *cond = Condition::Fog;
    setLabel(label, "Foggy");
  } else if (code >= 51 && code <= 57) {
    *cond = Condition::Rain;
    setLabel(label, "Drizzle");
  } else if (code >= 61 && code <= 67) {
    *cond = Condition::Rain;
    setLabel(label, "Rainy");
  } else if (code >= 71 && code <= 77) {
    *cond = Condition::Snow;
    setLabel(label, "Snowy");
  } else if (code >= 80 && code <= 82) {
    *cond = Condition::Rain;
    setLabel(label, "Showers");
  } else if (code == 85 || code == 86) {
    *cond = Condition::Snow;
    setLabel(label, "Snow");
  } else if (code >= 95) {
    *cond = Condition::Storm;
    setLabel(label, "Storm");
  } else {
    *cond = Condition::Unknown;
    setLabel(label, "Unknown");
  }
}

}  // namespace

const Data& current() { return s_data; }

bool fetch(double lat, double lon) {
  String url = "https://api.open-meteo.com/v1/forecast?latitude=";
  url += String(lat, 5);
  url += "&longitude=";
  url += String(lon, 5);
  url +=
      "&current=temperature_2m,apparent_temperature,weather_code"
      "&temperature_unit=fahrenheit";

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("weather: http.begin failed");
    return false;
  }

  http.setTimeout(10000);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("weather: HTTP %d\n", code);
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("weather: JSON parse error: %s\n", err.c_str());
    return false;
  }

  JsonObject cur = doc["current"].as<JsonObject>();
  if (cur.isNull()) {
    Serial.println("weather: no 'current' object");
    return false;
  }

  s_data.temp_f = cur["temperature_2m"].as<float>();
  s_data.feels_f = cur["apparent_temperature"].as<float>();
  const int wcode = cur["weather_code"].as<int>();
  classify(wcode, &s_data.condition, s_data.label);
  s_data.valid = true;

  Serial.printf("weather: %.0fF feels %.0fF code=%d (%s)\n", s_data.temp_f,
                s_data.feels_f, wcode, s_data.label);
  return true;
}

}  // namespace services::weather
