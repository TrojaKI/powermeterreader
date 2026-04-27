# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Purpose

Python tool to read and decode data from a **NÖ Netz Smart Meter P1 customer interface** (Kundenschnittstelle). The meter sends encrypted DLMS/COSEM frames every 5 seconds via wired M-Bus.

Two embedded implementations exist: ESP32/ESPHome (`esp32/`) and Arduino MKR WIFI 1010 (`arduino_mkr/`).

## Physical Interface

- **Connector**: RJ12 (6P6C), pins 3 = MBUS1(+), 4 = MBUS2(-)
- **Baud rate**: 2400 Baud
- **Transport**: M-Bus → USB/Serial adapter required (e.g. M-Bus to RS232/USB converter)
- **Direction**: unidirectional, meter sends only — no write access possible

## Protocol: DLMS/COSEM

- Standard: DLMS/COSEM Green Book Edition 9+, Interface Classes Book Edition 14+
- Security: **DLMS Security Suite 0** (AES-128 GCM, authenticated encryption)
- Each frame is symmetrically encrypted with a customer-specific **GUEK** (Global Unicast Encryption Key, 16 bytes)
- No Authentication Key (AK) is used
- Each frame contains an incrementing **Frame Counter** (4 bytes) — must be tracked to detect replay
- see https://www.weigu.lu/tutorials/sensors2bus/04_encryption/index.html

### APDU Frame Structure

```
[M-Bus Start] [System Title (8B)] [Frame Counter (4B)] [Encrypted Data] [M-Bus Checksum] [M-Bus Stop]
```

The System Title and Frame Counter are plaintext; the data payload is AES-128-GCM encrypted.

### Decryption Parameters

| Parameter | Value |
|-----------|-------|
| Algorithm | AES-128-GCM |
| Key | GUEK (16 bytes, provided by Netz NÖ via postal mail) |
| IV (nonce) | System Title (8B) + Frame Counter (4B) = 12 bytes |
| AAD | Security Control Byte + System Title |

## OBIS Data Fields (after decryption)

| OBIS Code | Description | Unit | Scale |
|-----------|-------------|------|-------|
| 1.0.1.8.0.255 | Wirkenergie A+ (consumed) | Wh | ×1 |
| 1.0.2.8.0.255 | Wirkenergie A- (fed in) | Wh | ×1 |
| 1.0.1.7.0.255 | Momentanleistung P+ | W | ×1 |
| 1.0.2.7.0.255 | Momentanleistung P- | W | ×1 |
| 1.0.32.7.0.255 | Spannung L1 | V | ×10⁻¹ |
| 1.0.52.7.0.255 | Spannung L2 | V | ×10⁻¹ |
| 1.0.72.7.0.255 | Spannung L3 | V | ×10⁻¹ |
| 1.0.31.7.0.255 | Strom L1 | A | ×10⁻² |
| 1.0.51.7.0.255 | Strom L2 | A | ×10⁻² |
| 1.0.71.7.0.255 | Strom L3 | A | ×10⁻² |
| 1.0.13.7.0.255 | Leistungsfaktor | — | ×10⁻³ |
| — | Zählernummer (meter serial) | — | ASCII in hex |

Timestamps are encoded as DLMS date-time octet strings (12 bytes including timezone offset).

## Key Implementation Notes

- The GUEK is secret — never hardcode in source, load from env or config file
- Frame Counter must be monotonically increasing; reject frames with lower/equal counter to prevent replay
- Single-phase meters (Wechselstromzähler S210) only populate L1; L2/L3 will be 0
- The decrypted payload is a DLMS `DataNotification` PDU — parse via the `NotificationBody > DataValue > Structure` path
- Enum `0x1E` = Wh, `0x1B` = W, `0x23` = V, `0x21` = A, `0xFF` = dimensionless

## Arduino MKR WIFI 1010 Implementation

Location: `arduino_mkr/stromzaehler/`

**Note:** Arduino MKR WIFI 1010 is **not** ESPHome-compatible. Uses pure Arduino framework with MQTT + HA auto-discovery.

### Hardware

| Component | Details |
|-----------|---------|
| Board | Arduino MKR WIFI 1010 (ATSAMD21G18A, 48 MHz, 3.3V logic) |
| WiFi | u-blox NINA-W102 via `WiFiNINA` library |
| M-Bus UART | `Serial1` at 2400 baud — TSS721A output → board **RX** pin |
| Home Assistant | MQTT auto-discovery (discovery topics sent once per MQTT connect) |

### Wiring (RJ12 → TSS721A → MKR WIFI 1010)

| RJ12 Pin | Signal | Connection |
|----------|--------|------------|
| 3 | MBUS1 (+) | TSS721A MBUS+ |
| 4 | MBUS2 (–) | TSS721A MBUS– |
| — | TSS721A RX out | Board **RX** pin (Serial1) |
| — | TSS721A GND | Board GND |
| — | TSS721A VCC | Board 3.3V |

### Required Libraries (Arduino Library Manager)

| Library | Author | Version |
|---------|--------|---------|
| WiFiNINA | Arduino | 1.8.x |
| PubSubClient | Nick O'Leary | 2.8.x |
| Crypto | Rhys Weatherley | 0.4.x |
| ArduinoOTA | Arduino | 1.0.x |

### Credentials

Copy `config.h.example` → `config.h`, fill in WIFI_SSID, WIFI_PASSWORD, MQTT_BROKER, GUEK_HEX, OTA_PASSWORD.
`config.h` is in `.gitignore` — never commit it.

### OTA Updates

After first USB flash: Tools → Port → select network port `stromzaehler at <IP>` → Upload.
OTA storage uses the NINA-W102 SPI flash (`WiFiStorage`) — **not** `InternalStorage` (SAMD21 has no dual-bank flash).

### MQTT Topics

| Topic | Retained | Content |
|-------|----------|---------|
| `homeassistant/sensor/stromzaehler_<field>/config` | yes | HA discovery payload (sent once on MQTT connect) |
| `stromzaehler/<field>` | yes | Current sensor value (every 5 s) |

## Reference Tool

Open source decrypter: `github.com/ric-geek/DLMS-APDU-Encrypter-Decrypter` (Python, entry point: `main_logic.py`) — useful for understanding the decryption flow, but not a dependency.
