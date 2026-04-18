import logging
from collections.abc import Generator

import serial

log = logging.getLogger(__name__)

# M-Bus frame markers
MBUS_START = 0x68
MBUS_STOP = 0x16
# Minimum plausible frame length (start + length + length + start + ctrl + addr + ci + stop + checksum)
MIN_FRAME_LEN = 9


def read_frames(port: str, baudrate: int = 2400) -> Generator[bytes, None, None]:
    """Yield complete M-Bus frames from the serial port."""
    with serial.Serial(port, baudrate=baudrate, bytesize=8, parity="N", stopbits=1, timeout=10) as ser:
        log.info("Opened serial port %s at %d baud", port, baudrate)
        buf = bytearray()
        while True:
            chunk = ser.read(256)
            if not chunk:
                continue
            buf.extend(chunk)
            while True:
                frame = _extract_frame(buf)
                if frame is None:
                    break
                yield bytes(frame)


def _extract_frame(buf: bytearray) -> bytes | None:
    """Extract and remove the first complete M-Bus frame from buf, or return None."""
    # Find start byte
    start = buf.find(MBUS_START)
    if start == -1:
        buf.clear()
        return None
    if start > 0:
        del buf[:start]

    # Need at least 4 bytes to read the length field (0x68 L L 0x68 ...)
    if len(buf) < 4:
        return None

    # M-Bus long frame: 0x68 L L 0x68 ... CS 0x16
    if buf[1] != buf[2]:
        # Length fields don't match — corrupt, skip this start byte
        del buf[0]
        return None

    frame_len = buf[1]
    total_len = 4 + frame_len + 2  # start(1) + L(1) + L(1) + start(1) + data(L) + CS(1) + stop(1)

    if len(buf) < total_len:
        return None  # not enough data yet

    if buf[total_len - 1] != MBUS_STOP:
        log.warning("Expected stop byte 0x16 at pos %d, got 0x%02x — skipping", total_len - 1, buf[total_len - 1])
        del buf[0]
        return None

    frame = bytes(buf[:total_len])
    del buf[:total_len]
    log.debug("Frame received: %d bytes", len(frame))
    return frame
