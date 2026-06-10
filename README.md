# Plane Radar + Weather (ESP32-S3 Touch)

Firmware for the **Waveshare ESP32-S3-Touch-LCD-1.28** — a 1.28″ round **GC9A01** display (240×240) with a **CST816 touchscreen**. Shows a circular **ADS-B radar** around your location, plus a **weather page** you reach by tapping the screen. **WiFiManager** handles first-time setup.

> **Fork note:** this is an ESP32-S3 port of [**ESP32-Plane-Radar** by MatixYo](https://github.com/MatixYo/ESP32-Plane-Radar) (MIT). The original targets an ESP32-C3 Super Mini; this fork retargets the Waveshare S3 touch board and adds a weather page and tap-to-switch. All original radar functionality is preserved.

## What it does

1. **Wi‑Fi setup** (if needed) — captive portal on AP **`PlaneRadar-Setup`**
2. **Radar** — live aircraft from [adsb.fi](https://opendata.adsb.fi/) on a sonar-style grid
3. **Weather** — current conditions from [Open-Meteo](https://open-meteo.com) (free, no API key)

**Tap anywhere on the screen to switch between the radar and the weather page; tap again to switch back.**

After Wi‑Fi is saved, the device reconnects automatically; the radar runs in the main loop with periodic ADS-B updates (~3 s), and the weather refreshes every 10 minutes while shown.

## Controls

| Input | Effect |
|-------|--------|
| **Tap screen** | Toggle between radar and weather page |
| **BOOT short tap** | Cycle range preset (5 → 10 → 15 → 25 km); saved to flash |
| **BOOT hold 3 s** | Clear Wi‑Fi, location, and units; reboot into setup portal |

BOOT is the on-board button on **GPIO 0** (active LOW). During setup you can also hold BOOT at power-on to force a credential reset.

## Weather page

Uses Open-Meteo's current-weather endpoint for your configured location (no API key, returns °F directly):

- **Condition** label (Sunny / Partly Cloudy / Cloudy / Foggy / Rainy / Snowy / Storm)
- **Color icon** for the condition
- **Temperature** in °F and **real feel** (apparent temperature) in °F

Layout and icons: `src/ui/weather_display.cpp`. Refresh interval: `kWeatherFetchIntervalMs` in `config.h`.

## Wi‑Fi setup portal

1. Connect to **`PlaneRadar-Setup`**
2. Open **`http://plane-radar.local`** (preferred) or **`http://192.168.4.1`** — both are shown on the yellow setup screen; the captive portal may open automatically
3. Set home Wi‑Fi, then save

**Custom fields** (stored in NVS):

| Field | Purpose |
|-------|---------|
| **Latitude / Longitude** | Radar center, ADS-B query position, **and weather location** |
| **Display distances in miles** | Ring scale label in **mi** instead of **km** |

## Radar display

- Dark background, subdued green rings and crosshairs; white **N / S / E / W** at the bezel and a range label on the east spoke
- **Inside the outer ring** — red heading triangle, magenta speed vector, callsign / type / altitude tags
- **Outside the ring** — small red dot on the rim at the correct bearing
- Position math uses a `cos(latitude)` correction so east–west distances are accurate away from the equator

### Range presets

| Ring 3 label | Outer radius (aircraft scale) |
|------------|-------------------------------|
| 5 km / 3 mi | ~6.7 km |
| 10 km / 6 mi | ~13.3 km (default) |
| 15 km / 9 mi | ~20 km |
| 25 km / 16 mi | ~33.3 km |

Preset and miles/km choice persist across reboot (`planeradar` NVS namespace).

## Wiring (Waveshare ESP32-S3-Touch-LCD-1.28)

The display and touch are on-board; no manual wiring is needed. Pins are defined in `include/config.h`:

| Function | GPIO |
|----------|------|
| Display SCLK | 10 |
| Display MOSI (SDA) | 11 |
| Display CS | 9 |
| Display DC | 8 |
| Display RST | 14 |
| Backlight (PWM) | 2 |
| Touch I²C SDA | 6 |
| Touch I²C SCL | 7 |
| Touch INT | 5 |
| Touch RST | 13 |
| BOOT button | 0 |

## Configuration

Edit **`include/config.h`** for hardware and behavior:

| Area | Keys / notes |
|------|----------------|
| Display SPI | pins, `kDisplayInvert`, `kDisplayRgbOrder`, `kDisplaySpiWriteHz` |
| Display offset | `kDisplayOffsetX/Y` — shift output to clear the bezel if needed |
| Backlight | brightness set in `src/hardware/display.cpp` via `setBrightness()` |
| Touch | `kTouchPinSda/Scl/Int/Rst`, `kTouchI2cAddr` |
| Weather | `kWeatherFetchIntervalMs`, `kWeatherRetryIntervalMs` |
| BOOT | `kBootPin`, `kBootResetHoldMs`, `kBootTapMinMs` |
| ADS-B | `kAdsbFetchIntervalMs`, `kAdsbShowGroundAircraft` |
| Default location | `kDefaultRadarLat`, `kDefaultRadarLon` (until portal overrides) |

Range presets: `include/ui/radar_range.h` (`kRangePresets`).

## Build & flash

```bash
pio run -e supermini -t upload --upload-port <PORT>   # e.g. COM3 on Windows
pio device monitor --port <PORT>
```

- PlatformIO env: **`supermini`** (board `esp32-s3-devkitc-1`)
- Serial: **115200** baud over the board's CH343 USB-UART bridge
- `ARDUINO_USB_CDC_ON_BOOT=0` so serial/logs come out the CH343 port

## Dependencies

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
- Weather data by [Open-Meteo](https://open-meteo.com); aircraft data by [adsb.fi](https://opendata.adsb.fi/)

## Credits & license

MIT. Original project © 2026 MatixYo — [ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar). ESP32-S3 port, touch, and weather page © 2026 TurboTime29. See [`LICENSE`](LICENSE).
