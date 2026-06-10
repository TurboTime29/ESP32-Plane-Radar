/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/touch.h"
#include "services/weather.h"
#include "services/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"
#include "ui/weather_display.h"

namespace {

enum class Page { Radar, Weather };

bool g_radar_visible = false;
Page g_page = Page::Radar;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;
unsigned long g_last_weather_fetch_ms = 0;

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

void onRangeTap() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    onRangeTap();
  }
}

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
    handleBootButton();
    return;
  }
  ui::radarDisplayRefreshAircraft();
  handleBootButton();
}

/** Fetch (if connected) and draw the weather page. */
void showWeather(bool force_fetch) {
  if (WiFi.status() == WL_CONNECTED &&
      (force_fetch || !services::weather::current().valid)) {
    services::weather::fetch(services::location::lat(),
                             services::location::lon());
    g_last_weather_fetch_ms = millis();
  }
  ui::weatherDisplayDraw();
}

/** A tap anywhere toggles between the radar and the weather page. */
void handleTouchToggle() {
  if (!services::touch::tapped()) {
    return;
  }
  if (g_page == Page::Radar) {
    g_page = Page::Weather;
    g_radar_visible = false;  // stop radar from drawing over the weather page
    showWeather(true);
  } else {
    g_page = Page::Radar;
    g_radar_visible = false;  // force a fresh radar redraw
    showRadarIfConnected();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");

  bootButtonInit();
  services::touch::init();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  ui::radar::rangeInit();

  if (wifiSetupConnect()) {
    showRadarIfConnected();
  }
}

void loop() {
  handleBootButton();
  handleTouchToggle();

  if (g_page == Page::Weather) {
    // Keep the weather page refreshed; retry sooner if we have no data yet.
    if (WiFi.status() == WL_CONNECTED) {
      const services::weather::Data& w = services::weather::current();
      const unsigned long since = millis() - g_last_weather_fetch_ms;
      if (since >= config::kWeatherFetchIntervalMs ||
          (!w.valid && since >= config::kWeatherRetryIntervalMs)) {
        showWeather(true);
      }
    }
    delay(10);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else if (millis() - g_last_adsb_fetch_ms >= config::kAdsbFetchIntervalMs) {
      g_last_adsb_fetch_ms = millis();
      fetchAndDrawAircraft();
    }
  }

  delay(10);
}
