# Smart Meter P1 Reader — NÖ Netz

Reads, decrypts, and forwards data from the **NÖ Netz Smart Meter P1 customer interface** (Kundenschnittstelle). The meter (Sagemcom T210D / S210) transmits encrypted DLMS/COSEM frames every 5 seconds via wired M-Bus.

## Implementations

Three variants exist — pick the one that fits your setup:

| Variant | Hardware needed | Requires PC | Home Assistant |
|---------|----------------|-------------|----------------|
| [Python (MBUS2USB)](#python-tool) | M-Bus → USB adapter + RJ12 cable | yes | via MQTT |
| [Arduino MKR WIFI 1010](arduino_mkr/README.md) | MKR board + MBUS Slave Shield + RJ12 | no (standalone) | MQTT auto-discovery |
| [ESP32 / ESPHome](esp32/README.md) | ESP32 dev board + M-Bus interface + RJ12 | no (standalone) | ESPHome native — ⚠️ experimental |

## Common Prerequisites

**GUEK encryption key** — 16 bytes (32 hex chars), issued by Netz NÖ per postal mail.
Request at: [netz-noe.at/services/kundenschnittstelle-aktivieren](https://netz-noe.at/services/kundenschnittstelle-aktivieren)

**Physical connection** — RJ12 cable from meter to your hardware:
- Pin 3 → MBUS+ (positive)
- Pin 4 → MBUS− (negative)

---

## Python Tool

A daemon that reads frames via an M-Bus→USB adapter and outputs to JSON, MQTT, and/or InfluxDB.

### Requirements

- Python 3.11+
- M-Bus to USB/Serial adapter (RJ12 → USB), e.g. Relay MBUS-USB

### Setup

```bash
python -m venv venv
source venv/bin/activate
pip install -r requirements.txt
cp .env.example .env
# Edit .env: set SERIAL_PORT and GUEK
```

### Run

```bash
python main.py
```

### Configuration

| Variable | Required | Default | Description |
|----------|----------|---------|-------------|
| `SERIAL_PORT` | yes | — | Serial device, e.g. `/dev/ttyUSB0` |
| `GUEK` | yes | — | 32 hex chars (AES-128 key from Netz NÖ) |
| `MQTT_HOST` | no | — | MQTT broker hostname |
| `MQTT_PORT` | no | `1883` | MQTT broker port |
| `MQTT_TOPIC_PREFIX` | no | `smartmeter` | Topic prefix |
| `INFLUX_URL` | no | — | InfluxDB 2.x URL |
| `INFLUX_TOKEN` | no | — | InfluxDB API token |
| `INFLUX_ORG` | no | — | InfluxDB organisation |
| `INFLUX_BUCKET` | no | — | InfluxDB bucket |

### MQTT Output

| Topic | Retained | Content |
|-------|----------|---------|
| `smartmeter/tele` | no | Full JSON payload (all fields + timestamp) |
| `smartmeter/<field>` | yes | Individual sensor value |

Fields: `energy_consumed_wh`, `energy_fed_wh`, `power_active_w`, `power_reactive_w`, `voltage_l1/l2/l3`, `current_l1/l2/l3`, `power_factor`, `meter_serial`

### Tests

```bash
pytest tests/ -v
```

### Debug Tool

`tools/apdu_decoder.py` — subscribes to raw MQTT hex frames and decodes them offline. Useful for protocol debugging without physical hardware.

---

## Protocol Reference

- Physical: RJ12, M-Bus, 2400 baud, 8E1
- Protocol: DLMS/COSEM (Green Book Ed. 9+, Interface Classes Book Ed. 14+)
- Encryption: AES-128-GCM (DLMS Security Suite 0), IV = System Title (8 B) + Frame Counter (4 B)
- The Sagemcom T210D splits one DLMS PDU across **two** M-Bus frames (256 B + 26 B)

Reference documents: [`docs/`](docs/)
