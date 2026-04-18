"""
DLMS/COSEM frame decoder for NÖ Netz Smart Meter P1 interface.

Frame structure (M-Bus long frame with General Ciphering):
  68 [L] [L] 68 [C] [A] [CI=0xDB] [System Title len=0x08] [System Title 8B]
  [ciphertext length 1-2B] [Security Control 1B] [Frame Counter 4B]
  [AES-128-GCM ciphertext + 12B tag] [CS] 16
"""
import logging
import struct
from datetime import datetime, timezone, timedelta

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

from smartmeter.models import MeterReading

log = logging.getLogger(__name__)

# OBIS code bytes -> field name, divisor
OBIS_MAP: dict[bytes, tuple[str, int]] = {
    bytes([1, 0, 1, 8, 0, 255]): ("energy_consumed_wh", 1),
    bytes([1, 0, 2, 8, 0, 255]): ("energy_fed_wh", 1),
    bytes([1, 0, 1, 7, 0, 255]): ("power_plus_w", 1),
    bytes([1, 0, 2, 7, 0, 255]): ("power_minus_w", 1),
    bytes([1, 0, 32, 7, 0, 255]): ("voltage_l1_v", 10),
    bytes([1, 0, 52, 7, 0, 255]): ("voltage_l2_v", 10),
    bytes([1, 0, 72, 7, 0, 255]): ("voltage_l3_v", 10),
    bytes([1, 0, 31, 7, 0, 255]): ("current_l1_a", 100),
    bytes([1, 0, 51, 7, 0, 255]): ("current_l2_a", 100),
    bytes([1, 0, 71, 7, 0, 255]): ("current_l3_a", 100),
    bytes([1, 0, 13, 7, 0, 255]): ("power_factor", 1000),
}

# DLMS type tags
TAG_OCTET_STRING = 0x09
TAG_DOUBLE_LONG_UNSIGNED = 0x06  # UInt32
TAG_LONG_UNSIGNED = 0x12         # UInt16


class DecodeError(Exception):
    pass


def decrypt_frame(raw: bytes, guek: bytes) -> bytes:
    """Decrypt a raw M-Bus frame and return the plaintext DLMS APDU bytes."""
    # M-Bus frame: 68 L L 68 C A [payload] CS 16
    # payload starts at index 6 (after 68 L L 68 C A)
    if len(raw) < 10 or raw[0] != 0x68 or raw[-1] != 0x16:
        raise DecodeError("Not a valid M-Bus frame")

    payload = raw[6:-2]  # strip header and CS+stop

    # General Ciphering header: CI=0xDB, system title length, system title
    if payload[0] != 0xDB:
        raise DecodeError(f"Expected CI=0xDB, got 0x{payload[0]:02x}")

    sys_title_len = payload[1]
    if sys_title_len != 8:
        raise DecodeError(f"Expected system title length 8, got {sys_title_len}")

    sys_title = payload[2:10]

    # Ciphertext length (may be 1 or 2 bytes: if first byte is 0x81, next byte is length)
    pos = 10
    if payload[pos] == 0x81:
        pos += 1
        cipher_len = payload[pos]
        pos += 1
    elif payload[pos] == 0x82:
        cipher_len = struct.unpack_from(">H", payload, pos + 1)[0]
        pos += 3
    else:
        cipher_len = payload[pos]
        pos += 1

    security_control = payload[pos]
    frame_counter = struct.unpack_from(">I", payload, pos + 1)[0]
    ciphertext_with_tag = payload[pos + 5: pos + cipher_len]

    iv = sys_title + struct.pack(">I", frame_counter)
    aad = bytes([security_control]) + sys_title

    try:
        aesgcm = AESGCM(guek)
        plaintext = aesgcm.decrypt(iv, ciphertext_with_tag, aad)
    except Exception as exc:
        # Try with empty AAD (some meter variants)
        try:
            plaintext = aesgcm.decrypt(iv, ciphertext_with_tag, b"")
        except Exception:
            raise DecodeError(f"AES-GCM decryption failed: {exc}") from exc

    log.debug(
        "Decrypted frame: sys_title=%s frame_counter=%d plaintext_len=%d",
        sys_title.hex(), frame_counter, len(plaintext),
    )
    return plaintext


def parse_apdu(data: bytes) -> MeterReading:
    """Parse a decrypted DLMS DataNotification APDU into a MeterReading."""
    # Skip DataNotification header: tag(1) + invoke-id-and-priority(4) + date-time(13)
    # date-time is encoded as: tag(0x09) + len(0x0C) + 12 bytes
    # We find the date-time and OBIS values via pattern search.

    timestamp = _parse_timestamp(data)
    meter_id = _parse_meter_id(data)
    values: dict[str, float] = {}

    for obis_bytes, (field, divisor) in OBIS_MAP.items():
        raw_val = _find_obis_value(data, obis_bytes)
        if raw_val is not None:
            values[field] = raw_val / divisor if divisor > 1 else raw_val

    return MeterReading(
        timestamp=timestamp,
        meter_id=meter_id,
        energy_consumed_wh=int(values.get("energy_consumed_wh", 0)),
        energy_fed_wh=int(values.get("energy_fed_wh", 0)),
        power_plus_w=int(values.get("power_plus_w", 0)),
        power_minus_w=int(values.get("power_minus_w", 0)),
        voltage_l1_v=values.get("voltage_l1_v", 0.0),
        voltage_l2_v=values.get("voltage_l2_v", 0.0),
        voltage_l3_v=values.get("voltage_l3_v", 0.0),
        current_l1_a=values.get("current_l1_a", 0.0),
        current_l2_a=values.get("current_l2_a", 0.0),
        current_l3_a=values.get("current_l3_a", 0.0),
        power_factor=values.get("power_factor", 0.0),
    )


def _find_obis_value(data: bytes, obis: bytes) -> int | None:
    """
    Find an OBIS code in data and return the following numeric value.
    Pattern: 09 06 [6 obis bytes] [06|12] [4 or 2 value bytes]
    """
    pattern = bytes([TAG_OCTET_STRING, 6]) + obis
    pos = data.find(pattern)
    if pos == -1:
        return None

    pos += len(pattern)
    if pos >= len(data):
        return None

    tag = data[pos]
    if tag == TAG_DOUBLE_LONG_UNSIGNED and pos + 5 <= len(data):
        return struct.unpack_from(">I", data, pos + 1)[0]
    if tag == TAG_LONG_UNSIGNED and pos + 3 <= len(data):
        return struct.unpack_from(">H", data, pos + 1)[0]
    return None


def _parse_timestamp(data: bytes) -> datetime:
    """
    Parse DLMS date-time octet string (12 bytes):
    year(2) month(1) day(1) dow(1) hour(1) min(1) sec(1) hundredths(1) tz_offset(2) clock_status(1)
    """
    # Find the 12-byte octet string: 09 0C [12 bytes]
    pattern = bytes([TAG_OCTET_STRING, 0x0C])
    pos = data.find(pattern)
    if pos == -1 or pos + 14 > len(data):
        return datetime.now(tz=timezone.utc)

    ts = data[pos + 2: pos + 14]
    year = struct.unpack_from(">H", ts, 0)[0]
    month, day, _, hour, minute, second = ts[2], ts[3], ts[4], ts[5], ts[6], ts[7]
    tz_raw = struct.unpack_from(">h", ts, 9)[0]  # signed, in minutes
    if tz_raw == -32768:  # 0x8000 = not specified
        tz = timezone.utc
    else:
        tz = timezone(timedelta(minutes=tz_raw))

    try:
        return datetime(year, month, day, hour, minute, second, tzinfo=tz)
    except ValueError:
        return datetime.now(tz=timezone.utc)


def _parse_meter_id(data: bytes) -> str:
    """
    Meter ID is the last octet string in the structure, encoded as ASCII hex.
    Pattern: 09 [len] [ASCII bytes for meter number]
    We look for the meter number near the end of the data.
    """
    # Meter ID is typically 12 ASCII chars = 09 0C [12 bytes]
    # Search from the end for the last 09 0C pattern
    pos = len(data) - 1
    while pos >= 2:
        if data[pos - 1] == TAG_OCTET_STRING and data[pos] > 4:
            length = data[pos]
            start = pos + 1
            end = start + length
            if end <= len(data):
                candidate = data[start:end]
                # Meter ID consists of printable ASCII digits
                if all(0x30 <= b <= 0x39 for b in candidate):
                    return candidate.decode("ascii")
        pos -= 1

    # Fallback: hex-decode the last octet string tagged value
    pattern = bytes([TAG_OCTET_STRING, 0x0C])
    pos = data.rfind(pattern)
    if pos != -1 and pos + 14 <= len(data):
        return data[pos + 2: pos + 14].hex()

    return "unknown"


def decode_frame(raw: bytes, guek: bytes) -> MeterReading:
    """Full pipeline: decrypt + parse a raw M-Bus frame."""
    plaintext = decrypt_frame(raw, guek)
    return parse_apdu(plaintext)
