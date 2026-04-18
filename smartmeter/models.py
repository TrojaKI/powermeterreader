from dataclasses import dataclass, asdict
from datetime import datetime


@dataclass
class MeterReading:
    timestamp: datetime
    meter_id: str
    energy_consumed_wh: int
    energy_fed_wh: int
    power_plus_w: int
    power_minus_w: int
    voltage_l1_v: float
    voltage_l2_v: float
    voltage_l3_v: float
    current_l1_a: float
    current_l2_a: float
    current_l3_a: float
    power_factor: float

    def to_dict(self) -> dict:
        d = asdict(self)
        d["timestamp"] = self.timestamp.isoformat()
        return d
