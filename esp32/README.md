# ESP32 / ESPHome — Smart Meter Reader

> **Experimental — work in progress.**
> The ESPHome external component (`external_components/stromzaehler/`) is implemented, but integration testing is incomplete. Use the [Arduino MKR variant](../arduino_mkr/README.md) for a production-ready embedded solution.

## Hardware

| Component | Details |
|-----------|---------|
| Board | ESP32 dev board (e.g. ESP32-DevKitC); set `board:` in `stromzaehler.yaml` |
| M-Bus interface | M-Bus to UART/TTL adapter; connect RX to GPIO16 (configurable in yaml) |
| M-Bus connection | RJ12 cable: pin 3 → MBUS+, pin 4 → MBUS− |

## Prerequisites

- [ESPHome](https://esphome.io/) installed (`pip install esphome` or via HA add-on)
- Home Assistant with ESPHome integration

## Configuration

Copy and edit the secrets file:

```bash
cp secrets.yaml.example secrets.yaml   # if example exists, otherwise create manually
```

`secrets.yaml` must define:

```yaml
wifi_ssid: "YourNetwork"
wifi_password: "YourPassword"
ha_api_key: "<32-byte base64 key>"     # generate: esphome generate-api-key
ota_password: "YourOtaPassword"
ap_password: "FallbackApPassword"
guek: "0123456789ABCDEF0123456789ABCDEF"  # 32 hex chars from Netz NÖ
```

`secrets.yaml` is in `.gitignore` — never commit it.

## Flash

```bash
cd esp32
esphome run stromzaehler.yaml
```

First flash requires USB. Subsequent updates use OTA (password from `secrets.yaml`).

A fallback WiFi AP (`Stromzaehler Fallback`) is activated automatically if the main network is unreachable.

## Sensors exposed to Home Assistant

All sensors are registered via the ESPHome API (no MQTT required):

- Energie Bezug / Einspeisung (kWh)
- Leistung Bezug / Einspeisung (W)
- Spannung L1/L2/L3 (V)
- Strom L1/L2/L3 (A)
- Leistungsfaktor
- Zählernummer
