#!/usr/bin/env python3
"""Decode DLMS DataNotification APDU from stromzaehler/debug/plain MQTT topic."""
import argparse
import struct
import sys
from datetime import datetime, timezone, timedelta

import paho.mqtt.client as mqtt

# OBIS code bytes -> (dotted notation, description)
OBIS_MAP: list[tuple[bytes, str, str]] = [
    (bytes([1, 0, 1, 8, 0, 255]),  "1.0.1.8.0.255",  "Energie Bezug"),
    (bytes([1, 0, 2, 8, 0, 255]),  "1.0.2.8.0.255",  "Energie Einspeisung"),
    (bytes([1, 0, 1, 7, 0, 255]),  "1.0.1.7.0.255",  "Leistung Bezug"),
    (bytes([1, 0, 2, 7, 0, 255]),  "1.0.2.7.0.255",  "Leistung Einspeisung"),
    (bytes([1, 0, 32, 7, 0, 255]), "1.0.32.7.0.255", "Spannung L1"),
    (bytes([1, 0, 52, 7, 0, 255]), "1.0.52.7.0.255", "Spannung L2"),
    (bytes([1, 0, 72, 7, 0, 255]), "1.0.72.7.0.255", "Spannung L3"),
    (bytes([1, 0, 31, 7, 0, 255]), "1.0.31.7.0.255", "Strom L1"),
    (bytes([1, 0, 51, 7, 0, 255]), "1.0.51.7.0.255", "Strom L2"),
    (bytes([1, 0, 71, 7, 0, 255]), "1.0.71.7.0.255", "Strom L3"),
    (bytes([1, 0, 13, 7, 0, 255]), "1.0.13.7.0.255", "Leistungsfaktor"),
]

UNIT_NAMES: dict[int, str] = {
    0x1E: "Wh",
    0x1B: "W",
    0x23: "V",
    0x21: "A",
    0x16: "VA",
    0x20: "Wh",   # some meter variants
    0xFF: "",
}


def parse_datetime(data: bytes) -> datetime | None:
    """Parse 12-byte DLMS DateTime starting at byte 6."""
    if len(data) < 18:
        return None
    ts = data[6:18]
    year = struct.unpack_from(">H", ts, 0)[0]
    month, day = ts[2], ts[3]
    hour, minute, second = ts[5], ts[6], ts[7]
    tz_raw = struct.unpack_from(">h", ts, 9)[0]  # signed, in minutes
    try:
        if tz_raw == -32768:
            tz = timezone.utc
        else:
            tz = timezone(timedelta(minutes=-tz_raw))  # Sagemcom encodes sign inverted
        return datetime(year, month, day, hour, minute, second, tzinfo=tz)
    except (ValueError, OverflowError):
        return None


def find_obis_entry(data: bytes, obis: bytes) -> tuple[int | None, int, int]:
    """
    Search for OBIS code and return (raw_value, scaler_int8, unit_enum).
    Pattern: 09 06 [6B OBIS] [06|12] [value] 02 02 0F [scaler] 16 [unit]
    """
    pattern = bytes([0x09, 0x06]) + obis
    pos = data.find(pattern)
    if pos == -1:
        return None, 0, 0xFF

    pos += 8  # skip 09 06 + 6 obis bytes

    if pos >= len(data):
        return None, 0, 0xFF

    tag = data[pos]
    if tag == 0x06 and pos + 5 <= len(data):    # UInt32
        value = struct.unpack_from(">I", data, pos + 1)[0]
        pos += 5
    elif tag == 0x12 and pos + 3 <= len(data):  # UInt16
        value = struct.unpack_from(">H", data, pos + 1)[0]
        pos += 3
    else:
        return None, 0, 0xFF

    # 02 02 0F [scaler] 16 [unit]
    if (pos + 5 < len(data)
            and data[pos] == 0x02
            and data[pos + 1] == 0x02
            and data[pos + 2] == 0x0F
            and data[pos + 4] == 0x16):
        scaler = struct.unpack_from("b", data, pos + 3)[0]  # signed Int8
        unit = data[pos + 5]
    else:
        scaler, unit = 0, 0xFF

    return value, scaler, unit


def parse_meter_serial(data: bytes) -> str:
    """Find last OctetString of printable ASCII digits = meter serial."""
    pos = len(data) - 1
    while pos >= 2:
        if data[pos - 1] == 0x09 and 4 <= data[pos] <= 20:
            length = data[pos]
            start = pos + 1
            end = start + length
            if end <= len(data):
                candidate = data[start:end]
                if all(0x20 <= b <= 0x7E for b in candidate):
                    return candidate.decode("ascii")
        pos -= 1
    return ""


def format_scaled(raw: int, scaler: int, unit: str) -> tuple[str, str]:
    """Return (display_value_str, annotation_str)."""
    if scaler == 0:
        return str(raw), ""
    scaled = raw * (10 ** scaler)
    if scaler < 0:
        decimals = -scaler
        val_str = f"{scaled:.{decimals}f}"
    else:
        val_str = f"{scaled:.0f}"
    annotation = f"(raw={raw} ×10⁻{abs(scaler)})" if scaler < 0 else f"(raw={raw} ×10⁺{scaler})"
    return val_str, annotation


def decode_apdu(data: bytes) -> None:
    """Parse and print the DLMS DataNotification APDU."""
    # Column widths
    W_OBIS, W_DESC, W_VAL, W_UNIT = 16, 22, 12, 5
    # Header row — measure actual width to size separator lines
    header = (f"{'OBIS Code':<{W_OBIS}}  {'Beschreibung':<{W_DESC}}"
              f"  {'Wert':>{W_VAL}}  {'Einheit':<{W_UNIT}}  Skalierung")
    WIDTH = len(header)
    bar  = "━" * WIDTH
    thin = "─" * WIDTH

    print(bar)

    if len(data) < 18:
        print(f"ERROR: APDU too short ({len(data)} bytes, need >= 18)")
        print(bar)
        return

    if data[0] != 0x0F:
        print(f"WARNING: Expected DataNotification tag 0x0F, got 0x{data[0]:02X}")

    invoke_id = struct.unpack_from(">I", data, 1)[0]
    ts = parse_datetime(data)
    ts_str = ts.strftime("%d.%m.%Y %H:%M:%S %z") if ts else "unknown"
    print(f"DLMS DataNotification  {ts_str}")
    print(f"{len(data)} Bytes Plaintext  (invoke-id: 0x{invoke_id:08X})")
    print(bar)

    print(header)
    print(thin)

    for obis_bytes, obis_str, description in OBIS_MAP:
        raw, scaler, unit_enum = find_obis_entry(data, obis_bytes)
        unit_name = UNIT_NAMES.get(unit_enum, f"0x{unit_enum:02X}")
        if raw is None:
            print(f"{obis_str:<{W_OBIS}}  {description:<{W_DESC}}  {'—':>{W_VAL}}  {unit_name:<{W_UNIT}}")
        else:
            val_str, annotation = format_scaled(raw, scaler, unit_name)
            print(f"{obis_str:<{W_OBIS}}  {description:<{W_DESC}}"
                  f"  {val_str:>{W_VAL}}  {unit_name:<{W_UNIT}}  {annotation}")

    serial = parse_meter_serial(data)
    if serial:
        print(thin)
        # label fills OBIS+DESC columns; serial right-aligns in value column
        label_w = W_OBIS + 2 + W_DESC
        print(f"{'Zählernummer:':<{label_w}}  {serial:>{W_VAL}}")

    print(bar)


def on_message(client: mqtt.Client, userdata: dict, msg: mqtt.MQTTMessage) -> None:
    hex_str = msg.payload.decode("ascii", errors="replace").strip()
    print(f"\nTopic: {msg.topic}  ({len(hex_str)} chars)")
    try:
        data = bytes.fromhex(hex_str)
    except ValueError as exc:
        print(f"ERROR: Invalid hex payload: {exc}")
        return

    decode_apdu(data)

    if userdata.get("once"):
        client.disconnect()


def main() -> None:
    parser = argparse.ArgumentParser(description="Decode DLMS APDU from MQTT debug topic")
    parser.add_argument("--broker", default="anemoi4", help="MQTT broker hostname")
    parser.add_argument("--port", type=int, default=1883, help="MQTT broker port")
    parser.add_argument("--topic", default="stromzaehler/debug/plain", help="MQTT topic")
    parser.add_argument("--once", action="store_true", help="Exit after first message")
    parser.add_argument("--hex", metavar="HEXSTRING", help="Decode hex string directly (no MQTT)")
    args = parser.parse_args()

    if args.hex:
        try:
            data = bytes.fromhex(args.hex.strip())
        except ValueError as exc:
            print(f"ERROR: Invalid hex string: {exc}", file=sys.stderr)
            sys.exit(1)
        decode_apdu(data)
        return

    client = mqtt.Client(userdata={"once": args.once})
    client.on_message = on_message
    client.connect(args.broker, args.port, keepalive=60)
    client.subscribe(args.topic)
    print(f"Listening on {args.broker}:{args.port} topic={args.topic} ...")
    client.loop_forever()


if __name__ == "__main__":
    main()
