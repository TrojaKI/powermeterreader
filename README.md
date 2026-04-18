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

## Protocol

NÖ Netz Smart Meter P1 interface (Kundenschnittstelle):
- Physical: RJ12, M-Bus, 2400 baud
- Protocol: DLMS/COSEM (Green Book Ed. 9+)
- Encryption: AES-128-GCM (DLMS Security Suite 0)
- Reference: [docs/](docs/)
