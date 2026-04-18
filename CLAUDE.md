# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Purpose

Python tool to read and decode data from a **NÖ Netz Smart Meter P1 customer interface** (Kundenschnittstelle). The meter sends encrypted DLMS/COSEM frames every 5 seconds via wired M-Bus.

In a later step implement a esp32 program.

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

## Reference Tool

Open source decrypter: `github.com/ric-geek/DLMS-APDU-Encrypter-Decrypter` (Python, entry point: `main_logic.py`) — useful for understanding the decryption flow, but not a dependency.
