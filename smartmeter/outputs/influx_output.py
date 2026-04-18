import logging

from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS

from smartmeter.config import Config
from smartmeter.models import MeterReading
from smartmeter.outputs import OutputBase

log = logging.getLogger(__name__)


class InfluxOutput(OutputBase):
    def __init__(self, config: Config) -> None:
        self._bucket = config.influx_bucket
        self._org = config.influx_org
        self._client = InfluxDBClient(
            url=config.influx_url,
            token=config.influx_token,
            org=config.influx_org,
        )
        self._write_api = self._client.write_api(write_options=SYNCHRONOUS)
        log.info("InfluxDB connected to %s", config.influx_url)

    def publish(self, reading: MeterReading) -> None:
        point = (
            Point("smartmeter")
            .tag("meter_id", reading.meter_id)
            .time(reading.timestamp, WritePrecision.SECONDS)
            .field("energy_consumed_wh", reading.energy_consumed_wh)
            .field("energy_fed_wh", reading.energy_fed_wh)
            .field("power_plus_w", reading.power_plus_w)
            .field("power_minus_w", reading.power_minus_w)
            .field("voltage_l1_v", reading.voltage_l1_v)
            .field("voltage_l2_v", reading.voltage_l2_v)
            .field("voltage_l3_v", reading.voltage_l3_v)
            .field("current_l1_a", reading.current_l1_a)
            .field("current_l2_a", reading.current_l2_a)
            .field("current_l3_a", reading.current_l3_a)
            .field("power_factor", reading.power_factor)
        )
        self._write_api.write(bucket=self._bucket, org=self._org, record=point)

    def close(self) -> None:
        self._client.close()
