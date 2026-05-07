# Arduino MKR WIFI 1010 — Smart Meter Reader

Standalone embedded variant. Runs 24/7 in the meter cabinet — no PC required. Publishes sensor data to Home Assistant via MQTT auto-discovery.

## Hardware

| Component | Details |
|-----------|---------|
| Board | Arduino MKR WIFI 1010 (ATSAMD21G18A, 48 MHz, 3.3 V logic) |
| M-Bus interface | [Arduino MKR MBUS Slave Shield](https://www.hwhardsoft.de/deutsch/projekte/m-bus-mkr-shield/) (HW Hardsoft, TSS721A, galvanically isolated) — plugs directly onto the MKR |
| M-Bus connection | RJ12 cable: pin 3 → Shield MBUS+, pin 4 → Shield MBUS− |
| Power | Shield is powered from the M-Bus line (24–36 V). MKR needs a separate USB-C 5 V supply. |

## Build

The project uses [PlatformIO](https://platformio.org/) (`platformio.ini` in this directory). Arduino IDE 2.x also works — select board **Arduino MKR WIFI 1010**.

### PlatformIO (recommended)

```bash
cd arduino_mkr
pio run                        # compile
pio run --target upload        # flash via USB
pio device monitor             # serial monitor at 115200 baud
```

### Arduino IDE

1. Board Manager → install **Arduino SAMD Boards**
2. Library Manager → install `WiFiNINA` (2.0.1), `PubSubClient`, `Crypto` (Rhys Weatherley)
3. Open `stromzaehler/stromzaehler.ino` → select board → Upload

## Configuration

Credentials are loaded from `~/Arduino/libraries/configuration/Configuration.h` — a shared library for all Arduino projects in this setup. It provides WiFi SSID/password, MQTT broker address/port, and `GUEK_HEX`.

Project-specific settings (MQTT client ID, publish interval) are in `stromzaehler/config.h`. A template is provided:

```bash
cp stromzaehler/config.h.example stromzaehler/stromzaehler/config.h
```

`config.h` is in `.gitignore` — never commit it.

### Key settings in `Configuration.h`

| Symbol | Description |
|--------|-------------|
| `s_ssid` | WiFi network name |
| `s_password` | WiFi password |
| `mqttServer` | MQTT broker hostname/IP |
| `mqttPort` | MQTT broker port (default 1883) |
| `GUEK_HEX` | 32 hex chars — AES-128 key from Netz NÖ |

## MQTT Topics

All values are published at most once per `PUBLISH_INTERVAL_MS` (default 30 s).

| Topic | Retained | Content |
|-------|----------|---------|
| `stromzaehler/energy_consumed_wh` | yes | Wirkenergie Bezug (kWh, 3 decimals) |
| `stromzaehler/energy_fed_wh` | yes | Wirkenergie Einspeisung (kWh, 3 decimals) |
| `stromzaehler/power_active_w` | yes | Momentanleistung Bezug (W) |
| `stromzaehler/power_reactive_w` | yes | Momentanleistung Einspeisung (W) |
| `stromzaehler/voltage_l1` | yes | Spannung L1 (V) |
| `stromzaehler/voltage_l2` | yes | Spannung L2 (V) |
| `stromzaehler/voltage_l3` | yes | Spannung L3 (V) |
| `stromzaehler/current_l1` | yes | Strom L1 (A) |
| `stromzaehler/current_l2` | yes | Strom L2 (A) |
| `stromzaehler/current_l3` | yes | Strom L3 (A) |
| `stromzaehler/power_factor` | yes | Leistungsfaktor |
| `stromzaehler/meter_serial` | yes | Zählernummer (ASCII) |
| `stromzaehler/debug` | no | Decrypt/parse error messages |
| `homeassistant/sensor/stromzaehler_<field>/config` | yes | HA discovery payload (sent once on MQTT connect) |

> Single-phase meters (S210) only populate L1 — L2/L3 will be `0.0`.

## Home Assistant

Sensors appear automatically via MQTT discovery — no manual HA configuration needed.
Requires the **MQTT integration** in Home Assistant pointing at the same broker.

## Web Interface

The firmware exposes a minimal status page at `http://<device-ip>/` showing the last decoded meter values and rx/frame counters. No authentication.

## OTA Updates

After the first USB flash, subsequent updates work over WiFi.

**Arduino IDE:** Tools → Port → select the network port (e.g. `stromzaehler at 192.168.x.x`) → Upload.

OTA storage uses the NINA-W102 SPI flash — **not** `InternalStorage` (the SAMD21 has no dual-bank flash). The ArduinoOTA library is therefore not used; OTA is handled via the web server (`web.cpp`).
