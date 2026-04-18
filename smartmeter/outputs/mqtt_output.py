import json
import logging

import paho.mqtt.client as mqtt

from smartmeter.config import Config
from smartmeter.models import MeterReading
from smartmeter.outputs import OutputBase

log = logging.getLogger(__name__)


class MqttOutput(OutputBase):
    def __init__(self, config: Config) -> None:
        self._prefix = config.mqtt_topic_prefix
        self._client = mqtt.Client()
        self._client.on_connect = self._on_connect
        self._client.connect(config.mqtt_host, config.mqtt_port, keepalive=60)
        self._client.loop_start()

    def _on_connect(self, client, userdata, flags, rc) -> None:
        if rc == 0:
            log.info("MQTT connected")
        else:
            log.error("MQTT connect failed: rc=%d", rc)

    def publish(self, reading: MeterReading) -> None:
        d = reading.to_dict()
        self._client.publish(f"{self._prefix}/tele", json.dumps(d), retain=False)
        for field, value in d.items():
            if field == "timestamp":
                continue
            self._client.publish(f"{self._prefix}/{field}", str(value), retain=True)

    def close(self) -> None:
        self._client.loop_stop()
        self._client.disconnect()
