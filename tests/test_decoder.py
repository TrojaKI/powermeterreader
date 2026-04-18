"""
Offline tests for the decoder using known DLMS data from the NÖ Netz P1 documentation.
"""
import struct
import pytest
from smartmeter.decoder import parse_apdu, _find_obis_value, _parse_meter_id, DecodeError, decrypt_frame


def _build_test_apdu() -> bytes:
    """
    Construct a minimal DataNotification APDU matching the PDF example (page 8).
    Values: energy_consumed=12937 Wh, voltage_L1=233.7V, pf=1.0, meter_id=181220000009
    """
    def octet_str(data: bytes) -> bytes:
        return bytes([0x09, len(data)]) + data

    def uint32(val: int) -> bytes:
        return bytes([0x06]) + struct.pack(">I", val)

    def uint16(val: int) -> bytes:
        return bytes([0x12]) + struct.pack(">H", val)

    def scaler_unit(scaler: int, unit: int) -> bytes:
        # Structure{2, Int8(scaler), Enum(unit)}
        s = scaler & 0xFF
        return bytes([0x02, 0x02, 0x0F, s, 0x16, unit])

    timestamp_bytes = bytes.fromhex("07E5091B01092F0F00FF8880")  # 27.09.2021 09:47:15+02:00

    items: list[bytes] = [
        octet_str(timestamp_bytes),
        # Wirkenergie A+ (1.0.1.8.0.255) = 12937 Wh
        octet_str(bytes([1, 0, 1, 8, 0, 255])) + uint32(12937) + scaler_unit(0, 0x1E),
        # Wirkenergie A- (1.0.2.8.0.255) = 0 Wh
        octet_str(bytes([1, 0, 2, 8, 0, 255])) + uint32(0) + scaler_unit(0, 0x1E),
        # Momentanleistung P+ (1.0.1.7.0.255) = 0 W
        octet_str(bytes([1, 0, 1, 7, 0, 255])) + uint32(0) + scaler_unit(0, 0x1B),
        # Momentanleistung P- (1.0.2.7.0.255) = 0 W
        octet_str(bytes([1, 0, 2, 7, 0, 255])) + uint32(0) + scaler_unit(0, 0x1B),
        # Spannung L1 (1.0.32.7.0.255) = 2337 (÷10 = 233.7 V)
        octet_str(bytes([1, 0, 32, 7, 0, 255])) + uint16(2337) + scaler_unit(-1, 0x23),
        # Spannung L2 (1.0.52.7.0.255) = 0
        octet_str(bytes([1, 0, 52, 7, 0, 255])) + uint16(0) + scaler_unit(-1, 0x23),
        # Spannung L3 (1.0.72.7.0.255) = 0
        octet_str(bytes([1, 0, 72, 7, 0, 255])) + uint16(0) + scaler_unit(-1, 0x23),
        # Strom L1 (1.0.31.7.0.255) = 0 A
        octet_str(bytes([1, 0, 31, 7, 0, 255])) + uint16(0) + scaler_unit(-2, 0x21),
        # Strom L2 (1.0.51.7.0.255) = 0 A
        octet_str(bytes([1, 0, 51, 7, 0, 255])) + uint16(0) + scaler_unit(-2, 0x21),
        # Strom L3 (1.0.71.7.0.255) = 0 A
        octet_str(bytes([1, 0, 71, 7, 0, 255])) + uint16(0) + scaler_unit(-2, 0x21),
        # Leistungsfaktor (1.0.13.7.0.255) = 1000 (÷1000 = 1.0)
        octet_str(bytes([1, 0, 13, 7, 0, 255])) + uint16(1000) + scaler_unit(-3, 0xFF),
        # Zählernummer = "181220000009" (ASCII)
        octet_str(b"181220000009"),
    ]
    body = b"".join(items)

    # DataNotification PDU: tag(0F) + invoke-id(4B) + datetime-header + Structure(body)
    datetime_field = bytes([0x0C]) + timestamp_bytes  # length=12 + 12 bytes
    struct_wrapper = bytes([0x02, 35]) + body          # Structure, 35 items
    return bytes([0x0F]) + bytes.fromhex("8006870E") + datetime_field + struct_wrapper


APDU_BYTES = _build_test_apdu()


def test_parse_energy_consumed():
    reading = parse_apdu(APDU_BYTES)
    assert reading.energy_consumed_wh == 12937


def test_parse_voltage_l1():
    reading = parse_apdu(APDU_BYTES)
    assert reading.voltage_l1_v == pytest.approx(233.7, rel=1e-3)


def test_parse_power_factor():
    reading = parse_apdu(APDU_BYTES)
    assert reading.power_factor == pytest.approx(1.0, rel=1e-3)


def test_parse_meter_id():
    reading = parse_apdu(APDU_BYTES)
    assert reading.meter_id == "181220000009"


def test_invalid_frame_raises():
    with pytest.raises(DecodeError):
        decrypt_frame(b"\x00" * 20, b"\x00" * 16)
