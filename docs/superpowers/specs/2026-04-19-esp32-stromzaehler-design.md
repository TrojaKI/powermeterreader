# ESP32 Stromzähler — Design Spec

**Date:** 2026-04-19
**Status:** Approved

## Problem

Das bestehende Python-Tool liest NÖ Netz Smart Meter Daten via USB/Serial am PC.
Ziel ist ein eigenständiger ESP32, der dauerhaft im Zählerkasten läuft und Daten
direkt an Home Assistant Yellow liefert — ohne PC, ohne Server.

## Constraints

- Zählerkasten eingemauert, keine Kabelführung möglich
- WiFi-Router ~2m vom Kasten → WiFi-Test vor finalem Hardware-Entscheid
- Fallback: ESP32-S3 mit U.FL-Anschluss + 4mm Loch in Tür
- Stromversorgung via USB 5V im Kasten
- OTA-Updates zwingend (kein einfacher physischer Zugang)

## Hardware

| Komponente | Zweck |
|---|---|
| ESP32 (TTGO für Test) | Hauptrechner |
| M-Bus Slave Modul (TSS721A) | Level-Shifter M-Bus 24V → UART 3.3V |
| USB 5V Netzteil | Dauerstrom |
| RJ12 Kabel (Pin 3=MBUS+, Pin 4=MBUS-) | Verbindung zum Meter |

## Architecture

```
[NÖ Netz Meter RJ12]
  → [M-Bus Slave Modul TSS721A]  (24V → 3.3V UART)
  → [ESP32 GPIO16 UART RX, 2400 baud]
  → [ESPHome Custom C++ Component]
       ├── MBusReader     (Frame sync + buffer)
       ├── AESDecryptor   (mbedTLS AES-128-GCM)
       └── DLMSParser     (OBIS extraction)
  → [WiFi] → [HA Yellow ESPHome Integration]
       └── 12 Sensor Entities + 1 TextSensor
```

## File Layout

```
esp32/
├── components/stromzaehler/
│   ├── __init__.py          # ESPHome registration
│   ├── stromzaehler.h
│   └── stromzaehler.cpp
├── stromzaehler.yaml        # ESPHome config
└── secrets.yaml             # GUEK (never commit!)
```

## Data Flow

```
UART RX (2400 baud, 5s interval)
  → MBusReader: buffer until 0x68...0x16 frame complete
  → Frame Counter: reject if <= last_counter (replay protection)
  → AESDecryptor:
       IV  = SystemTitle[8B] + FrameCounter[4B]
       AAD = SecurityControlByte + SystemTitle
       KEY = GUEK (from secrets.yaml)
       → mbedtls_gcm_crypt_and_tag()
  → DLMSParser: DataNotification → Structure → OBIS map
  → sensor->publish_state() × 12
```

## Sensor Entities

| Entity | OBIS | Unit | Scale |
|---|---|---|---|
| energy_consumed | 1.0.1.8.0.255 | Wh | ×1 |
| energy_fed | 1.0.2.8.0.255 | Wh | ×1 |
| power_active | 1.0.1.7.0.255 | W | ×1 |
| power_reactive | 1.0.2.7.0.255 | W | ×1 |
| voltage_l1 | 1.0.32.7.0.255 | V | ×0.1 |
| voltage_l2 | 1.0.52.7.0.255 | V | ×0.1 |
| voltage_l3 | 1.0.72.7.0.255 | V | ×0.1 |
| current_l1 | 1.0.31.7.0.255 | A | ×0.01 |
| current_l2 | 1.0.51.7.0.255 | A | ×0.01 |
| current_l3 | 1.0.71.7.0.255 | A | ×0.01 |
| power_factor | 1.0.13.7.0.255 | — | ×0.001 |
| meter_serial | — | — | ASCII (TextSensor) |

## Error Handling

- GCM auth tag invalid → `ESP_LOGW`, discard frame
- UART timeout (>10s no frame) → buffer reset
- Unknown OBIS → skip, continue parsing
- Frame counter replay → `ESP_LOGW`, discard

## Security

- GUEK only in `secrets.yaml` — never hardcoded, never logged
- `secrets.yaml` in `.gitignore`

## Verification

1. Serial Monitor: rohe Hex-Frames sichtbar → MBusReader OK
2. Decrypted payload == Python-Referenz → AESDecryptor OK
3. Spannung L1 ~230V, Leistung plausibel → DLMSParser OK
4. Alle 12 Entities in HA, Update alle 5s → Integration OK
5. OTA-Flash ohne Kabelzugang → Deployment OK
