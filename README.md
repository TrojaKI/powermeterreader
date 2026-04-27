# Smart Meter P1 Reader

Python daemon that reads, decrypts, and forwards data from the NÖ Netz Smart Meter P1 customer interface (Kundenschnittstelle).

## Requirements

- M-Bus to USB/Serial adapter (RJ12 → USB)
- GUEK encryption key (16 bytes, obtained from Netz NÖ via postal mail)
- Python 3.11+

## Setup

```bash
python -m venv venv
source venv/bin/activate
pip install -r requirements.txt
cp .env.example .env
# Edit .env: set SERIAL_PORT and GUEK
```

## Run

```bash
python main.py
```

## Configuration

| Variable | Required | Description |
|----------|----------|-------------|
| `SERIAL_PORT` | yes | Serial device, e.g. `/dev/ttyUSB0` |
| `GUEK` | yes | 32 hex chars (AES-128 key from Netz NÖ) |
| `MQTT_HOST` | no | MQTT broker hostname |
| `MQTT_PORT` | no | MQTT broker port (default: 1883) |
| `MQTT_TOPIC_PREFIX` | no | Topic prefix (default: `smartmeter`) |
| `INFLUX_URL` | no | InfluxDB 2.x URL |
| `INFLUX_TOKEN` | no | InfluxDB API token |
| `INFLUX_ORG` | no | InfluxDB organisation |
| `INFLUX_BUCKET` | no | InfluxDB bucket |

## Output

The meter sends a frame every 5 seconds. Each frame is logged as JSON and optionally published to MQTT and InfluxDB.

MQTT topics:
- `smartmeter/tele` — full JSON payload
- `smartmeter/<field>` — individual retained values

## Tests

```bash
pytest tests/ -v
```

## Arduino MKR WIFI 1010

Standalone embedded variant — runs 24/7 in the meter cabinet with direct Home Assistant integration, no PC required.

### Hardware Required

- Arduino MKR WIFI 1010
- M-Bus Slave Module (TSS721A or compatible) — level-shifts 24V M-Bus to 3.3V UART
- RJ12 cable (pin 3 = MBUS+, pin 4 = MBUS–)
- USB-C 5V power supply

### Setup

1. Install [Arduino IDE 2.x](https://www.arduino.cc/en/software)
2. Tools → Board Manager → search `Arduino SAMD` → install
3. Install libraries (Tools → Manage Libraries):
   `WiFiNINA`, `PubSubClient`, `Crypto` (by Rhys Weatherley), `ArduinoOTA`
4. Copy config:
   ```bash
   cp arduino_mkr/stromzaehler/config.h.example arduino_mkr/stromzaehler/config.h
   ```
5. Edit `config.h` — set WIFI_SSID, WIFI_PASSWORD, MQTT_BROKER, GUEK_HEX (32 hex chars from Netz NÖ), OTA_PASSWORD
6. Open `arduino_mkr/stromzaehler/stromzaehler.ino` in Arduino IDE
7. Select board: Tools → Board → Arduino SAMD Boards → **Arduino MKR WIFI 1010**
8. Upload via USB

### Home Assistant

Sensors appear automatically via MQTT discovery — no manual configuration needed.
Requires MQTT integration enabled in HA with the broker address set in `config.h`.

### OTA Updates

After first USB flash, subsequent updates via WiFi:
Tools → Port → select network port → Upload.
Password: `OTA_PASSWORD` from `config.h`.

## Protocol

NÖ Netz Smart Meter P1 interface (Kundenschnittstelle):
- Physical: RJ12, M-Bus, 2400 baud
- Protocol: DLMS/COSEM (Green Book Ed. 9+)
- Encryption: AES-128-GCM (DLMS Security Suite 0)
- Reference: [docs/](docs/)
