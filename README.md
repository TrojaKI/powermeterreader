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

Subscribing to the topic "stromzaehler/debug/plain" shows the raw ADPU (random chars below)

```bash
$ mosquitto_sub -h anemoi4 -t "stromzaehler/debug/plain" -v
stromzaehler/debug/plain A6AD0E5A1C5EE96A01FE4A4BDF9E88D78AEDBF07E9E5DE0D429FDB1F3038CB9FB33C23603F527E5B07786CCFDDB7A9D9C3639BD7882E7B3EF1C30799774359B9FC0C24B62EBCB0B5781F1EB1877EACF02FC641C0573F690DE0967717BB3A8CCDABB9B0B7619361100668D60BF138A53C844B21FD467704819D75A5644D3D703AC6E6354BC0F3BC537CA3837A26B7899A5BBD938C2D47CDA8025E807DBA3D3B6A933DF1BFF1609772D57452B9A281332F69B5FE207E7BB9DFC0172822373DB75665AB199B430C7BF08B6D86A3FD67CCD28EB29953D2B661776689BF236EEEFC257D1B58E7EC4125E00615C169F75327171F4E050000000000000000000000000
```


Example Usage
```bash
./venv/bin/python tools/apdu_decoder.py --broker anemoi4 --topic "stromzaehler/debug/plain"
Listening on anemoi4:1883 topic=stromzaehler/debug/plain ...

Topic: stromzaehler/debug/plain  (486 chars)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
DLMS DataNotification  07.05.2026 17:50:50 +0200
243 Bytes Plaintext  (invoke-id: 0x80151C33)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
OBIS Code         Beschreibung                    Wert  Einheit  Skalierung
───────────────────────────────────────────────────────────────────────────
1.0.1.8.0.255     Energie Bezug               17823213  Wh
1.0.2.8.0.255     Energie Einspeisung             3596  Wh
1.0.1.7.0.255     Leistung Bezug                   375  W
1.0.2.7.0.255     Leistung Einspeisung               0  W
1.0.32.7.0.255    Spannung L1                    233.4  V      (raw=2334 ×10⁻1)
1.0.52.7.0.255    Spannung L2                    232.5  V      (raw=2325 ×10⁻1)
1.0.72.7.0.255    Spannung L3                    234.3  V      (raw=2343 ×10⁻1)
1.0.31.7.0.255    Strom L1                        0.93  A      (raw=93 ×10⁻2)
1.0.51.7.0.255    Strom L2                        0.42  A      (raw=42 ×10⁻2)
1.0.71.7.0.255    Strom L3                        1.21  A      (raw=121 ×10⁻2)
1.0.13.7.0.255    Leistungsfaktor                0.628         (raw=628 ×10⁻3)
───────────────────────────────────────────────────────────────────────────
Zählernummer:                             123456789000
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

### Homeassistant

#### discovery

```bash
$ mosquitto_sub -h anemoi4 -t "homeassistant/sensor/#" | jq "."
{
  "name": "Energie Bezug",
  "state_topic": "stromzaehler/energy_consumed_wh",
  "unit_of_measurement": "kWh",
  "device_class": "energy",
  "state_class": "total_increasing",
  "suggested_display_precision": 3,
  "unique_id": "smkr_energy_consumed_wh",
  "device": {
    "identifiers": [
      "stromzaehler_mkr"
    ],
    "name": "NÖ Netz Stromzähler",
    "model": "Arduino MKR WIFI 1010",
    "sw_version": "1.0.2"
  }
}
{
  "name": "Energie Einspeisung",
  "state_topic": "stromzaehler/energy_fed_wh",
  "unit_of_measurement": "kWh",
  "device_class": "energy",
  "state_class": "total_increasing",
  "suggested_display_precision": 3,
  "unique_id": "smkr_energy_fed_wh",
  "device": {
    "identifiers": [
      "stromzaehler_mkr"
    ],
    "name": "NÖ Netz Stromzähler",
    "model": "Arduino MKR WIFI 1010",
    "sw_version": "1.0.2"
  }
}
{
  "name": "Leistung Bezug",
  "state_topic": "stromzaehler/power_active_w",
  "unit_of_measurement": "W",
  "device_class": "power",
  "state_class": "measurement",
  "suggested_display_precision": 0,
  "unique_id": "smkr_power_active_w",
  "device": {
    "identifiers": [
      "stromzaehler_mkr"
    ],
    "name": "NÖ Netz Stromzähler",
    "model": "Arduino MKR WIFI 1010",
    "sw_version": "1.0.2"
  }
}
{
  "name": "Leistung Einsp.",
  "state_topic": "stromzaehler/power_reactive_w",
  "unit_of_measurement": "W",
  "device_class": "power",
  "state_class": "measurement",
  "suggested_display_precision": 0,
  "unique_id": "smkr_power_reactive_w",
  "device": {
    "identifiers": [
      "stromzaehler_mkr"
    ],
    "name": "NÖ Netz Stromzähler",
    "model": "Arduino MKR WIFI 1010",
    "sw_version": "1.0.2"
  }
}
{
  "name": "Spannung L1",
  "state_topic": "stromzaehler/voltage_l1",
  "unit_of_measurement": "V",
  "device_class": "voltage",
  "state_class": "measurement",
  "suggested_display_precision": 1,
  "unique_id": "smkr_voltage_l1",
  "device": {
    "identifiers": [
      "stromzaehler_mkr"
    ],
    "name": "NÖ Netz Stromzähler",
    "model": "Arduino MKR WIFI 1010",
    "sw_version": "1.0.2"
  }
}
{
  "name": "Spannung L2",
  "state_topic": "stromzaehler/voltage_l2",
  "unit_of_measurement": "V",
  "device_class": "voltage",
  "state_class": "measurement",
  "suggested_display_precision": 1,
  "unique_id": "smkr_voltage_l2",
  "device": {
    "identifiers": [
      "stromzaehler_mkr"
    ],
    "name": "NÖ Netz Stromzähler",
    "model": "Arduino MKR WIFI 1010",
    "sw_version": "1.0.2"
  }
}
{
  "name": "Spannung L3",
  "state_topic": "stromzaehler/voltage_l3",
  "unit_of_measurement": "V",
  "device_class": "voltage",
  "state_class": "measurement",
  "suggested_display_precision": 1,
  "unique_id": "smkr_voltage_l3",
  "device": {
    "identifiers": [
      "stromzaehler_mkr"
    ],
    "name": "NÖ Netz Stromzähler",
    "model": "Arduino MKR WIFI 1010",
    "sw_version": "1.0.2"
  }
}
{
  "name": "Strom L1",
  "state_topic": "stromzaehler/current_l1",
  "unit_of_measurement": "A",
  "device_class": "current",
  "state_class": "measurement",
  "suggested_display_precision": 2,
  "unique_id": "smkr_current_l1",
  "device": {
    "identifiers": [
      "stromzaehler_mkr"
    ],
    "name": "NÖ Netz Stromzähler",
    "model": "Arduino MKR WIFI 1010",
    "sw_version": "1.0.2"
  }
}
{
  "name": "Strom L2",
  "state_topic": "stromzaehler/current_l2",
  "unit_of_measurement": "A",
  "device_class": "current",
  "state_class": "measurement",
  "suggested_display_precision": 2,
  "unique_id": "smkr_current_l2",
  "device": {
    "identifiers": [
      "stromzaehler_mkr"
    ],
    "name": "NÖ Netz Stromzähler",
    "model": "Arduino MKR WIFI 1010",
    "sw_version": "1.0.2"
  }
}
{
  "name": "Strom L3",
  "state_topic": "stromzaehler/current_l3",
  "unit_of_measurement": "A",
  "device_class": "current",
  "state_class": "measurement",
  "suggested_display_precision": 2,
  "unique_id": "smkr_current_l3",
  "device": {
    "identifiers": [
      "stromzaehler_mkr"
    ],
    "name": "NÖ Netz Stromzähler",
    "model": "Arduino MKR WIFI 1010",
    "sw_version": "1.0.2"
  }
}
{
  "name": "Leistungsfaktor",
  "state_topic": "stromzaehler/power_factor",
  "unit_of_measurement": "",
  "device_class": "power_factor",
  "state_class": "measurement",
  "suggested_display_precision": 3,
  "unique_id": "smkr_power_factor",
  "device": {
    "identifiers": [
      "stromzaehler_mkr"
    ],
    "name": "NÖ Netz Stromzähler",
    "model": "Arduino MKR WIFI 1010",
    "sw_version": "1.0.2"
  }
}
{
  "name": "Zählernummer",
  "state_topic": "stromzaehler/meter_serial",
  "unique_id": "smkr_meter_serial",
  "device": {
    "identifiers": [
      "stromzaehler_mkr"
    ],
    "name": "NÖ Netz Stromzähler",
    "model": "Arduino MKR WIFI 1010",
    "sw_version": "1.0.2"
  }
}
```

#### sample MQTT data
```bash
$ mosquitto_sub -h anemoi4 -t "stromzaehler/#" -v
stromzaehler/energy_consumed_wh 17823.244
stromzaehler/energy_fed_wh 3.596
stromzaehler/power_active_w 369
stromzaehler/power_reactive_w 0
stromzaehler/voltage_l1 234.5
stromzaehler/voltage_l2 232.2
stromzaehler/voltage_l3 235.5
stromzaehler/current_l1 0.93
stromzaehler/current_l2 0.42
stromzaehler/current_l3 1.19
stromzaehler/power_factor 0.616
stromzaehler/meter_serial 123456789000
```


---

## Protocol Reference

- Physical: RJ12, M-Bus, 2400 baud, 8E1
- Protocol: DLMS/COSEM (Green Book Ed. 9+, Interface Classes Book Ed. 14+)
- Encryption: AES-128-GCM (DLMS Security Suite 0), IV = System Title (8 B) + Frame Counter (4 B)
- The Sagemcom T210D splits one DLMS PDU across **two** M-Bus frames (256 B + 26 B)

Reference documents: [`docs/`](docs/)
