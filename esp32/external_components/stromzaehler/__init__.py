import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, sensor, text_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_VOLTAGE,
    DEVICE_CLASS_CURRENT,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_WATT_HOURS,
    UNIT_WATT,
    UNIT_VOLT,
    UNIT_AMPERE,
)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "text_sensor"]

stromzaehler_ns = cg.esphome_ns.namespace("stromzaehler")
StromzaehlerComponent = stromzaehler_ns.class_(
    "StromzaehlerComponent", cg.Component, uart.UARTDevice
)

CONF_GUEK = "guek"
CONF_ENERGY_CONSUMED = "energy_consumed"
CONF_ENERGY_FED = "energy_fed"
CONF_POWER_ACTIVE = "power_active"
CONF_POWER_REACTIVE = "power_reactive"
CONF_VOLTAGE_L1 = "voltage_l1"
CONF_VOLTAGE_L2 = "voltage_l2"
CONF_VOLTAGE_L3 = "voltage_l3"
CONF_CURRENT_L1 = "current_l1"
CONF_CURRENT_L2 = "current_l2"
CONF_CURRENT_L3 = "current_l3"
CONF_POWER_FACTOR = "power_factor"
CONF_METER_SERIAL = "meter_serial"

_SENSOR = sensor.sensor_schema
_TEXT = text_sensor.text_sensor_schema

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(StromzaehlerComponent),
            cv.Required(CONF_GUEK): cv.string,
            cv.Optional(CONF_ENERGY_CONSUMED): _SENSOR(
                unit_of_measurement=UNIT_WATT_HOURS,
                device_class=DEVICE_CLASS_ENERGY,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_ENERGY_FED): _SENSOR(
                unit_of_measurement=UNIT_WATT_HOURS,
                device_class=DEVICE_CLASS_ENERGY,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_POWER_ACTIVE): _SENSOR(
                unit_of_measurement=UNIT_WATT,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_POWER_REACTIVE): _SENSOR(
                unit_of_measurement=UNIT_WATT,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_VOLTAGE_L1): _SENSOR(
                unit_of_measurement=UNIT_VOLT,
                device_class=DEVICE_CLASS_VOLTAGE,
                state_class=STATE_CLASS_MEASUREMENT,
                accuracy_decimals=1,
            ),
            cv.Optional(CONF_VOLTAGE_L2): _SENSOR(
                unit_of_measurement=UNIT_VOLT,
                device_class=DEVICE_CLASS_VOLTAGE,
                state_class=STATE_CLASS_MEASUREMENT,
                accuracy_decimals=1,
            ),
            cv.Optional(CONF_VOLTAGE_L3): _SENSOR(
                unit_of_measurement=UNIT_VOLT,
                device_class=DEVICE_CLASS_VOLTAGE,
                state_class=STATE_CLASS_MEASUREMENT,
                accuracy_decimals=1,
            ),
            cv.Optional(CONF_CURRENT_L1): _SENSOR(
                unit_of_measurement=UNIT_AMPERE,
                device_class=DEVICE_CLASS_CURRENT,
                state_class=STATE_CLASS_MEASUREMENT,
                accuracy_decimals=2,
            ),
            cv.Optional(CONF_CURRENT_L2): _SENSOR(
                unit_of_measurement=UNIT_AMPERE,
                device_class=DEVICE_CLASS_CURRENT,
                state_class=STATE_CLASS_MEASUREMENT,
                accuracy_decimals=2,
            ),
            cv.Optional(CONF_CURRENT_L3): _SENSOR(
                unit_of_measurement=UNIT_AMPERE,
                device_class=DEVICE_CLASS_CURRENT,
                state_class=STATE_CLASS_MEASUREMENT,
                accuracy_decimals=2,
            ),
            cv.Optional(CONF_POWER_FACTOR): _SENSOR(
                state_class=STATE_CLASS_MEASUREMENT,
                accuracy_decimals=3,
            ),
            cv.Optional(CONF_METER_SERIAL): _TEXT(),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_guek_hex(config[CONF_GUEK]))

    for conf_key, setter in (
        (CONF_ENERGY_CONSUMED,  "set_energy_consumed_sensor"),
        (CONF_ENERGY_FED,       "set_energy_fed_sensor"),
        (CONF_POWER_ACTIVE,     "set_power_active_sensor"),
        (CONF_POWER_REACTIVE,   "set_power_reactive_sensor"),
        (CONF_VOLTAGE_L1,       "set_voltage_l1_sensor"),
        (CONF_VOLTAGE_L2,       "set_voltage_l2_sensor"),
        (CONF_VOLTAGE_L3,       "set_voltage_l3_sensor"),
        (CONF_CURRENT_L1,       "set_current_l1_sensor"),
        (CONF_CURRENT_L2,       "set_current_l2_sensor"),
        (CONF_CURRENT_L3,       "set_current_l3_sensor"),
        (CONF_POWER_FACTOR,     "set_power_factor_sensor"),
    ):
        if conf_key in config:
            sens = await sensor.new_sensor(config[conf_key])
            cg.add(getattr(var, setter)(sens))

    if CONF_METER_SERIAL in config:
        ts = await text_sensor.new_text_sensor(config[CONF_METER_SERIAL])
        cg.add(var.set_meter_serial_sensor(ts))
