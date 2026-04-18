import os
import logging
from dataclasses import dataclass, field

from dotenv import load_dotenv

load_dotenv()

log = logging.getLogger(__name__)


@dataclass
class Config:
    serial_port: str
    guek: bytes
    mqtt_host: str | None
    mqtt_port: int
    mqtt_topic_prefix: str
    influx_url: str | None
    influx_token: str | None
    influx_org: str | None
    influx_bucket: str | None


def load_config() -> Config:
    serial_port = os.environ.get("SERIAL_PORT", "/dev/ttyUSB0")

    guek_hex = os.environ.get("GUEK", "")
    if not guek_hex or len(guek_hex) != 32:
        raise ValueError("GUEK must be set as 32 hex characters (16 bytes)")
    guek = bytes.fromhex(guek_hex)

    mqtt_host = os.environ.get("MQTT_HOST")
    mqtt_port = int(os.environ.get("MQTT_PORT", "1883"))
    mqtt_topic_prefix = os.environ.get("MQTT_TOPIC_PREFIX", "smartmeter")

    influx_url = os.environ.get("INFLUX_URL")
    influx_token = os.environ.get("INFLUX_TOKEN")
    influx_org = os.environ.get("INFLUX_ORG")
    influx_bucket = os.environ.get("INFLUX_BUCKET")

    log.info("Config loaded: port=%s, mqtt=%s, influx=%s", serial_port, mqtt_host, influx_url)
    return Config(
        serial_port=serial_port,
        guek=guek,
        mqtt_host=mqtt_host,
        mqtt_port=mqtt_port,
        mqtt_topic_prefix=mqtt_topic_prefix,
        influx_url=influx_url,
        influx_token=influx_token,
        influx_org=influx_org,
        influx_bucket=influx_bucket,
    )
