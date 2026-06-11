#include "services/weather.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cstring>

namespace services::weather {

namespace {

Data s_data;

// Cached US National Weather Service observation endpoint for the current
// location, so we only do the point->station lookup when the location changes.
String s_obs_url;
double s_station_lat = 1e9;
double s_station_lon = 1e9;

// NWS requires a descriptive User-Agent on every request.
constexpr char kUserAgent[] = "ESP32-PlaneRadar (github.com/TurboTime29)";

void setLabel(char* out, const char* text) {
  strncpy(out, text, sizeof(Data::label) - 1);
  out[sizeof(Data::label) - 1] = '\0';
}

// Wraps an HTTP body stream so reads block for the next TCP segment instead of
// reporting EOF on a momentary gap. Lets ArduinoJson stream-parse large chunked
// responses (the NWS station list is ~57 KB) without buffering the whole body.
class BlockingStream : public Stream {
 public:
  BlockingStream(Stream& inner, uint32_t timeout_ms)
      : inner_(inner), timeout_ms_(timeout_ms) {}
  int available() override { return inner_.available(); }
  int read() override { return wait() ? inner_.read() : -1; }
  int peek() override { return wait() ? inner_.peek() : -1; }
  size_t write(uint8_t) override { return 0; }

 private:
  bool wait() {
    const uint32_t start = millis();
    while (inner_.available() == 0) {
      if (millis() - start > timeout_ms_) {
        return false;
      }
      delay(1);
    }
    return true;
  }
  Stream& inner_;
  uint32_t timeout_ms_;
};

float cToF(float c) { return c * 9.0f / 5.0f + 32.0f; }

// ---- US National Weather Service (api.weather.gov) ----

/** GET `url` and parse the JSON body through `filter` (streamed, low memory). */
bool nwsGet(const String& url, JsonDocument& doc, const JsonDocument& filter) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    return false;
  }
  http.setUserAgent(kUserAgent);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("Accept-Encoding", "identity");  // no gzip; we parse plain text
  http.setTimeout(15000);

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("weather(nws): HTTP %d\n", code);
    http.end();
    return false;
  }

  // Stream-parse through a blocking wrapper (waits for each TCP segment) with a
  // filter, so the 57 KB station list never has to fit in RAM at once.
  BlockingStream blocking(http.getStream(), 12000);
  const DeserializationError err =
      deserializeJson(doc, blocking, DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("weather(nws): JSON parse error: %s\n", err.c_str());
    return false;
  }
  return true;
}

/** Map an NWS textDescription (e.g. "Mostly Cloudy") to an icon class. */
void classifyNws(const char* desc, Condition* cond, char* label) {
  setLabel(label, (desc && desc[0]) ? desc : "Weather");

  String d(desc ? desc : "");
  d.toLowerCase();
  const auto has = [&](const char* k) { return d.indexOf(k) >= 0; };

  if (has("thunder")) {
    *cond = Condition::Storm;
  } else if (has("snow") || has("sleet") || has("ice") || has("flurr")) {
    *cond = Condition::Snow;
  } else if (has("rain") || has("drizzle") || has("shower")) {
    *cond = Condition::Rain;
  } else if (has("fog") || has("haze") || has("mist") || has("smoke")) {
    *cond = Condition::Fog;
  } else if (has("partly")) {
    *cond = Condition::PartlyCloudy;  // "Partly Cloudy/Sunny"
  } else if (has("cloudy") || has("overcast")) {
    *cond = Condition::Cloudy;        // "Mostly Cloudy", "Cloudy", "Overcast"
  } else if (has("clear") || has("sunny") || has("fair")) {
    *cond = Condition::Clear;         // incl. "Mostly Clear"
  } else {
    *cond = Condition::Cloudy;
  }
}

/** GET `url` and stream-scan for the first "stationIdentifier":"XXXX" value.
 *  Avoids parsing the ~57 KB station FeatureCollection just to read one id. */
bool nwsScanStationId(const String& url, char* out, size_t out_len) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    return false;
  }
  http.setUserAgent(kUserAgent);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("Accept-Encoding", "identity");
  http.setTimeout(15000);
  if (http.GET() != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  Stream& s = http.getStream();
  const char* key = "\"stationIdentifier\"";  // property name; value follows ": "
  const size_t klen = strlen(key);
  size_t match = 0;
  size_t n = 0;
  int state = 0;  // 0 = match key, 1 = await value's opening quote, 2 = capture
  bool found = false;
  uint32_t last = millis();

  while (millis() - last < 12000) {
    if (s.available() == 0) {
      if (!http.connected()) {
        break;  // body fully received
      }
      delay(1);
      continue;
    }
    last = millis();
    const int ci = s.read();
    if (ci < 0) {
      continue;
    }
    const char c = static_cast<char>(ci);
    if (state == 0) {
      if (c == key[match]) {
        if (++match == klen) {
          state = 1;
        }
      } else {
        match = (c == key[0]) ? 1 : 0;
      }
    } else if (state == 1) {
      if (c == '"') {  // skip the ':' and any spaces
        state = 2;
      }
    } else if (c == '"') {
      out[n] = '\0';
      found = n > 0;
      break;
    } else if (n < out_len - 1) {
      out[n++] = c;
    }
  }
  http.end();
  return found;
}

/** Resolve and cache the nearest observation station for this location. */
bool nwsResolveStation(double lat, double lon) {
  String purl = "https://api.weather.gov/points/" + String(lat, 4) + "," +
                String(lon, 4);
  JsonDocument pfilter;
  pfilter["properties"]["observationStations"] = true;
  JsonDocument pdoc;
  if (!nwsGet(purl, pdoc, pfilter)) {
    return false;
  }
  const char* stations_url =
      pdoc["properties"]["observationStations"].as<const char*>();
  if (stations_url == nullptr) {
    return false;
  }

  char sid[12];
  if (!nwsScanStationId(String(stations_url), sid, sizeof(sid))) {
    return false;
  }

  s_obs_url = String("https://api.weather.gov/stations/") + sid +
              "/observations/latest";
  s_station_lat = lat;
  s_station_lon = lon;
  Serial.printf("weather(nws): station %s\n", sid);
  return true;
}

bool fetchNws(double lat, double lon) {
  if (s_obs_url.length() == 0 || lat != s_station_lat ||
      lon != s_station_lon) {
    if (!nwsResolveStation(lat, lon)) {
      return false;
    }
  }

  JsonDocument filter;
  JsonObject fp = filter["properties"].to<JsonObject>();
  fp["temperature"]["value"] = true;
  fp["heatIndex"]["value"] = true;
  fp["windChill"]["value"] = true;
  fp["textDescription"] = true;

  JsonDocument doc;
  if (!nwsGet(s_obs_url, doc, filter)) {
    s_obs_url = "";  // force a fresh station lookup next time
    return false;
  }

  JsonObject p = doc["properties"];
  if (p["temperature"]["value"].isNull()) {
    return false;
  }
  const float temp_c = p["temperature"]["value"].as<float>();

  float feels_c = temp_c;  // NWS reports heatIndex or windChill only when apt
  if (!p["heatIndex"]["value"].isNull()) {
    feels_c = p["heatIndex"]["value"].as<float>();
  } else if (!p["windChill"]["value"].isNull()) {
    feels_c = p["windChill"]["value"].as<float>();
  }

  s_data.temp_f = cToF(temp_c);
  s_data.feels_f = cToF(feels_c);
  classifyNws(p["textDescription"].as<const char*>(), &s_data.condition,
              s_data.label);
  s_data.valid = true;
  Serial.printf("weather(nws): %.0fF feels %.0fF (%s)\n", s_data.temp_f,
                s_data.feels_f, s_data.label);
  return true;
}

// ---- Open-Meteo fallback (worldwide, no US station required) ----

/** Map WMO weather interpretation codes (Open-Meteo) to a class + label. */
void classifyOpenMeteo(int code, Condition* cond, char* label) {
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

bool fetchOpenMeteo(double lat, double lon) {
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
  classifyOpenMeteo(cur["weather_code"].as<int>(), &s_data.condition,
                    s_data.label);
  s_data.valid = true;

  Serial.printf("weather(open-meteo): %.0fF feels %.0fF (%s)\n", s_data.temp_f,
                s_data.feels_f, s_data.label);
  return true;
}

}  // namespace

const Data& current() { return s_data; }

bool fetch(double lat, double lon) {
  // Prefer NWS (US, matches Apple/National Weather Service); fall back to
  // Open-Meteo anywhere NWS has no coverage or is unreachable.
  if (fetchNws(lat, lon)) {
    return true;
  }
  Serial.println("weather: NWS unavailable, falling back to Open-Meteo");
  return fetchOpenMeteo(lat, lon);
}

}  // namespace services::weather
