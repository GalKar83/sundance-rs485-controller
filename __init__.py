import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, sensor, binary_sensor, text_sensor
from esphome import pins
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
)

DEPENDENCIES = ["uart"]

AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor", "button", "climate", "switch", "select"]

sundance_spa_ns = cg.esphome_ns.namespace("sundance_spa")
SundanceSpa = sundance_spa_ns.class_("SundanceSpa", cg.Component, uart.UARTDevice)
TempScale = sundance_spa_ns.enum("TempScale", is_class=True)

CONF_SUNDANCE_SPA_ID = "sundance_spa_id"
CONF_TEMP_SENSOR = "temp_sensor"
CONF_HEATER_SENSOR = "heater_binary_sensor"
CONF_CIRC_SENSOR = "circ_binary_sensor"
CONF_REST_SENSOR = "rest_binary_sensor"
CONF_CIRC_STATUS_TEXT_SENSOR = "circ_status_text_sensor"
CONF_LIGHT_STATUS_TEXT_SENSOR = "light_status_text_sensor"
CONF_TEMPERATURE_SCALE = "temperature_scale"
CONF_FLOW_CONTROL_PIN = "flow_control_pin"
CONF_RX_INVERT = "rx_invert"
CONF_COMMUNICATION_SENSOR = "communication_sensor"

TEMP_SCALE = {
    "F": TempScale.FAHRENHEIT,
    "C": TempScale.CELSIUS,
    "FAHRENHEIT": TempScale.FAHRENHEIT,
    "CELSIUS": TempScale.CELSIUS,
}

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SundanceSpa),
            cv.Optional(CONF_TEMPERATURE_SCALE, default="F"): cv.enum(TEMP_SCALE, upper=True),
            cv.Optional(CONF_FLOW_CONTROL_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_RX_INVERT, default=False): cv.boolean,
            cv.Optional(CONF_TEMP_SENSOR): sensor.sensor_schema(
                device_class=DEVICE_CLASS_TEMPERATURE,
                accuracy_decimals=1,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_CIRC_SENSOR): binary_sensor.binary_sensor_schema(),
            cv.Optional(CONF_HEATER_SENSOR): binary_sensor.binary_sensor_schema(),
            cv.Optional(CONF_REST_SENSOR): binary_sensor.binary_sensor_schema(),
            cv.Optional(CONF_CIRC_STATUS_TEXT_SENSOR): text_sensor.text_sensor_schema(),
            # Off / On / Seeking
            cv.Optional(CONF_LIGHT_STATUS_TEXT_SENSOR): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_COMMUNICATION_SENSOR): binary_sensor.binary_sensor_schema(),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_temperature_scale(config[CONF_TEMPERATURE_SCALE]))
    cg.add(var.set_rx_invert(config[CONF_RX_INVERT]))

    if CONF_FLOW_CONTROL_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_FLOW_CONTROL_PIN])
        cg.add(var.set_flow_control_pin(pin))

    if CONF_TEMP_SENSOR in config:
        conf = dict(config[CONF_TEMP_SENSOR])
        scale = config.get(CONF_TEMPERATURE_SCALE, "F")
        if scale in ("C", "CELSIUS"):
            conf["unit_of_measurement"] = "°C"
        else:
            conf["unit_of_measurement"] = "°F"
        sens = await sensor.new_sensor(conf)
        cg.add(var.set_temp_sensor(sens))

    if CONF_CIRC_SENSOR in config:
        conf = config[CONF_CIRC_SENSOR]
        sens = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_circ_binary_sensor(sens))

    if CONF_HEATER_SENSOR in config:
        conf = config[CONF_HEATER_SENSOR]
        sens = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_heater_binary_sensor(sens))

    if CONF_REST_SENSOR in config:
        conf = config[CONF_REST_SENSOR]
        sens = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_rest_binary_sensor(sens))

    if CONF_CIRC_STATUS_TEXT_SENSOR in config:
        conf = config[CONF_CIRC_STATUS_TEXT_SENSOR]
        sens = await text_sensor.new_text_sensor(conf)
        cg.add(var.set_circ_status_text_sensor(sens))

    if CONF_LIGHT_STATUS_TEXT_SENSOR in config:
        conf = config[CONF_LIGHT_STATUS_TEXT_SENSOR]
        sens = await text_sensor.new_text_sensor(conf)
        cg.add(var.set_light_status_text_sensor(sens))

    if CONF_COMMUNICATION_SENSOR in config:
        conf = config[CONF_COMMUNICATION_SENSOR]
        sens = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_communication_binary_sensor(sens))
