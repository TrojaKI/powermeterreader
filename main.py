#!/usr/bin/env python3
import logging
import signal
import sys

from smartmeter.config import load_config
from smartmeter.decoder import decode_frame, DecodeError
from smartmeter.outputs import OutputBase
from smartmeter.outputs.log_output import LogOutput
from smartmeter.reader import read_frames

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s %(message)s",
    stream=sys.stdout,
)
log = logging.getLogger(__name__)

_running = True


def _shutdown(signum, frame) -> None:
    global _running
    log.info("Shutdown signal received")
    _running = False


def main() -> None:
    signal.signal(signal.SIGTERM, _shutdown)
    signal.signal(signal.SIGINT, _shutdown)

    config = load_config()
    outputs: list[OutputBase] = [LogOutput()]

    if config.mqtt_host:
        from smartmeter.outputs.mqtt_output import MqttOutput
        outputs.append(MqttOutput(config))

    if config.influx_url:
        from smartmeter.outputs.influx_output import InfluxOutput
        outputs.append(InfluxOutput(config))

    log.info("Starting Smart Meter reader on %s with %d output(s)", config.serial_port, len(outputs))
    try:
        for raw_frame in read_frames(config.serial_port):
            if not _running:
                break
            try:
                reading = decode_frame(raw_frame, config.guek)
            except DecodeError as exc:
                log.warning("Frame decode error: %s", exc)
                continue
            for out in outputs:
                try:
                    out.publish(reading)
                except Exception as exc:
                    log.error("Output error (%s): %s", type(out).__name__, exc)
    finally:
        for out in outputs:
            out.close()
        log.info("Shutdown complete")


if __name__ == "__main__":
    main()
