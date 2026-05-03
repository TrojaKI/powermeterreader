"""
DLMS/COSEM frame decoder for NÖ Netz Smart Meter P1 interface (Sagemcom T210D).

Frame structure (validated vs github.com/FKW9/esp-smartmeter-netznoe):
  Frame1 (256B): 68 FA FA 68 53 FF 00 01 67 DB 08 [sys_title 8B]
                 81 F8 [sec_ctrl] [inv_ctr 4B] [ciphertext 228B] CS 16
  Frame2  (26B): 68 14 14 68 53 FF 11 01 67 [ciphertext 15B] CS 16
  AES-128-GCM, empty AAD, no auth-tag.
"""
import logging
import struct
from datetime import datetime, timezone, timedelta

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

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

# Fixed frame offsets (absolute from frame start, Sagemcom T210D)
_OFF_CI        =  9   # CI=0xDB General Ciphering
_OFF_SYS_TITLE = 11   # sys_title (8 bytes)
_OFF_INV_CTR   = 22   # frame counter (4 bytes, big-endian)
_OFF_CIPHER_F1 = 26   # ciphertext start in frame1
_OFF_CIPHER_F2 =  9   # ciphertext start in frame2


class DecodeError(Exception):
    pass


_last_fc: int = 0
_frame1_pending: bytes | None = None


def _aes_gcm_decrypt_no_tag(key: bytes, iv: bytes, ciphertext: bytes) -> bytes:
    # GCM data blocks = CTR starting at J0+1 where J0 = nonce||0x00000001 (12B nonce)
    counter = iv + b'\x00\x00\x00\x02'
    dec = Cipher(algorithms.AES(key), modes.CTR(counter)).decryptor()
    return dec.update(ciphertext) + dec.finalize()


def decrypt_frame(raw: bytes, guek: bytes, frame2: bytes | None = None) -> bytes:
    """Decrypt a Sagemcom T210D two-frame M-Bus sequence; frame2 is the 26B continuation."""
    global _last_fc

    if len(raw) < _OFF_CIPHER_F1 + 2 or raw[0] != 0x68 or raw[-1] != 0x16:
        raise DecodeError(f"Invalid frame1 ({len(raw)}B)")
    if raw[_OFF_CI] != 0xDB:
        raise DecodeError(f"Expected CI=0xDB at offset {_OFF_CI}, got 0x{raw[_OFF_CI]:02x}")

    iv = raw[_OFF_SYS_TITLE:_OFF_SYS_TITLE + 8] + raw[_OFF_INV_CTR:_OFF_INV_CTR + 4]
    fc = struct.unpack_from(">I", iv, 8)[0]
    if fc <= _last_fc:
        raise DecodeError(f"Replay detected: fc={fc} <= last={_last_fc}")

    # Combine ciphertext from both frames (strip CS+stop = last 2 bytes each)
    cipher1 = raw[_OFF_CIPHER_F1:-2]
    if frame2 is not None:
        if len(frame2) < _OFF_CIPHER_F2 + 2:
            raise DecodeError(f"Frame2 too short ({len(frame2)}B)")
        cipher2 = frame2[_OFF_CIPHER_F2:-2]
    else:
        cipher2 = b""

    plaintext = _aes_gcm_decrypt_no_tag(guek, iv, cipher1 + cipher2)
    _last_fc = fc
    log.debug("Decrypted: fc=%d plain=%dB", fc, len(plaintext))
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


def decode_frame(raw: bytes, guek: bytes) -> MeterReading | None:
    """
    Stateful decode: buffers the 256B frame1, decrypts on receipt of the 26B frame2.
    Returns None when frame1 is buffered (awaiting frame2).
    """
    global _frame1_pending

    if len(raw) == 256:
        _frame1_pending = raw
        log.debug("Frame1 buffered — waiting for frame2")
        return None

    if len(raw) == 26 and _frame1_pending is not None:
        f1 = _frame1_pending
        _frame1_pending = None
        return parse_apdu(decrypt_frame(f1, guek, frame2=raw))

    _frame1_pending = None
    raise DecodeError(f"Unexpected frame length {len(raw)}B (expected 256 then 26)")
