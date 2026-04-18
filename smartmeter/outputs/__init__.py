from abc import ABC, abstractmethod
from smartmeter.models import MeterReading


class OutputBase(ABC):
    @abstractmethod
    def publish(self, reading: MeterReading) -> None: ...

    def close(self) -> None:
        pass
