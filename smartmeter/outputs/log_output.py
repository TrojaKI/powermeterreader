import json
import logging

from smartmeter.models import MeterReading
from smartmeter.outputs import OutputBase

log = logging.getLogger(__name__)


class LogOutput(OutputBase):
    def publish(self, reading: MeterReading) -> None:
        log.info("meter_reading %s", json.dumps(reading.to_dict()))
