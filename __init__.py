import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, sensor, binary_sensor, text_sensor, time
from esphome import pins
from esphome.const import (
    CONF_ID,
    ENTITY_CATEGORY_DIAGNOSTIC,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
)

DEPENDENCIES = ["uart"]

AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor", "button", "climate", "switch", "select", "time"]

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
CONF_PUMPS_RUNNING_SENSOR = "pumps_running_binary_sensor"
CONF_CIRC_AUTO_SENSOR = "circulation_auto_binary_sensor"
CONF_PUMP1_RUNNING_SENSOR = "pump1_running_binary_sensor"
CONF_PUMP2_RUNNING_SENSOR = "pump2_running_binary_sensor"
# 680-series service counters and clock (decoded from the C4 status frame)
CONF_H2O_CHANGE_SENSOR = "h2o_change_sensor"
CONF_FILTER_CHANGE_SENSOR = "filter_change_sensor"
CONF_UV_CHANGE_SENSOR = "uv_change_sensor"
# raw status bytes for decoding work (diagnostic, numeric so they stay out of the logbook)
CONF_RAW_STATUS_SENSORS = "raw_status_sensors"
CONF_FIELD = "field"
CONF_SCREEN_KEY_SENSOR = "screen_key_sensor"
CONF_JET1_RUN_SENSOR = "jet1_run_time_sensor"
CONF_JET2_RUN_SENSOR = "jet2_run_time_sensor"
CONF_LIGHT_RUN_SENSOR = "light_run_time_sensor"
CONF_CIRC_RUN_SENSOR = "circulation_run_time_sensor"
CONF_HEATER_RUN_SENSOR = "heater_run_time_sensor"
CONF_ACTIVITY = "activity"
CONF_ITEM = "item"
CONF_ACTIVE = "active"
CONF_SESSION = "session"
CONF_SESSION_MIN = "session_min"
ACTIVITY_ITEMS = {
    "jet1": 0,
    "jet2": 1,
    "light": 2,
    "circulation": 3,
    "heater": 4,
    "spa_in_use": 5,
    "heating_run": 6,
    "filter_cycle": 7,
}
CONF_PANEL_BUTTON_SENSOR = "panel_button_sensor"
CONF_RAW_LIGHTS_TEXT_SENSOR = "raw_lights_text_sensor"
CONF_MSG_TYPES_TEXT_SENSOR = "msg_types_text_sensor"
CONF_RAW_OTHER_TEXT_SENSOR = "raw_other_text_sensor"
CONF_TIME_TEXT_SENSOR = "time_text_sensor"
CONF_HEATER_MODE_TEXT_SENSOR = "heater_mode_text_sensor"
CONF_CLEARRAY_TEXT_SENSOR = "clearray_text_sensor"
CONF_LANGUAGE_TEXT_SENSOR = "language_text_sensor"
CONF_LOCK_TEXT_SENSOR = "lock_text_sensor"
CONF_TEMP_FORMAT_TEXT_SENSOR = "temp_format_text_sensor"
CONF_DATE_TEXT_SENSOR = "date_text_sensor"
CONF_CLOCK_SYNC_TEXT_SENSOR = "clock_sync_text_sensor"
CONF_DECODER_NOTICE_TEXT_SENSOR = "decoder_notice_text_sensor"
CONF_TIME_ID = "time_id"
CONF_CLOCK_AUTO_SYNC = "clock_auto_sync"
CONF_PF_START_TEXT_SENSOR = "pf_start_text_sensor"
CONF_PF_CYCLES_SENSOR = "pf_cycles_sensor"
CONF_PF_DURATION_SENSOR = "pf_duration_sensor"

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
            # Any pump running (Jet 1 low/high, Jet 2) / circulation started by the controller itself
            cv.Optional(CONF_PUMPS_RUNNING_SENSOR): binary_sensor.binary_sensor_schema(icon="mdi:pump"),
            cv.Optional(CONF_CIRC_AUTO_SENSOR): binary_sensor.binary_sensor_schema(icon="mdi:sync"),
            cv.Optional(CONF_PUMP1_RUNNING_SENSOR): binary_sensor.binary_sensor_schema(icon="mdi:pump"),
            cv.Optional(CONF_PUMP2_RUNNING_SENSOR): binary_sensor.binary_sensor_schema(icon="mdi:pump"),
            # Service counters as text: days remaining, "Change Now" at 0, or "Off" when disabled.
            cv.Optional(CONF_H2O_CHANGE_SENSOR): text_sensor.text_sensor_schema(icon="mdi:water-sync"),
            cv.Optional(CONF_FILTER_CHANGE_SENSOR): text_sensor.text_sensor_schema(icon="mdi:air-filter"),
            cv.Optional(CONF_UV_CHANGE_SENSOR): text_sensor.text_sensor_schema(
                icon="mdi:lightbulb-fluorescent-tube"
            ),
            cv.Optional(CONF_RAW_STATUS_SENSORS): cv.ensure_list(
                sensor.sensor_schema(
                    accuracy_decimals=0,
                    state_class=STATE_CLASS_MEASUREMENT,
                    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                    icon="mdi:numeric",
                ).extend({cv.Required(CONF_FIELD): cv.int_range(min=0, max=15)})
            ),
            cv.Optional(CONF_JET1_RUN_SENSOR): sensor.sensor_schema(
                unit_of_measurement="min", accuracy_decimals=0, state_class=STATE_CLASS_MEASUREMENT, icon="mdi:timer-outline"
            ),
            cv.Optional(CONF_JET2_RUN_SENSOR): sensor.sensor_schema(
                unit_of_measurement="min", accuracy_decimals=0, state_class=STATE_CLASS_MEASUREMENT, icon="mdi:timer-outline"
            ),
            cv.Optional(CONF_LIGHT_RUN_SENSOR): sensor.sensor_schema(
                unit_of_measurement="min", accuracy_decimals=0, state_class=STATE_CLASS_MEASUREMENT, icon="mdi:timer-outline"
            ),
            cv.Optional(CONF_CIRC_RUN_SENSOR): sensor.sensor_schema(
                unit_of_measurement="min", accuracy_decimals=0, state_class=STATE_CLASS_MEASUREMENT, icon="mdi:timer-outline"
            ),
            cv.Optional(CONF_HEATER_RUN_SENSOR): sensor.sensor_schema(
                unit_of_measurement="min", accuracy_decimals=0, state_class=STATE_CLASS_MEASUREMENT, icon="mdi:timer-outline"
            ),
            cv.Optional(CONF_ACTIVITY): cv.ensure_list(
                cv.Schema(
                    {
                        cv.Required(CONF_ITEM): cv.enum(ACTIVITY_ITEMS, lower=True),
                        cv.Optional(CONF_ACTIVE): binary_sensor.binary_sensor_schema(icon="mdi:play-circle-outline"),
                        cv.Optional(CONF_SESSION): binary_sensor.binary_sensor_schema(icon="mdi:counter"),
                        cv.Optional(CONF_SESSION_MIN, default="0s"): cv.positive_time_period_seconds,
                    }
                )
            ),
            cv.Optional(CONF_SCREEN_KEY_SENSOR): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:key-variant",
            ),
            cv.Optional(CONF_PANEL_BUTTON_SENSOR): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon="mdi:gesture-tap-button"
            ),
            cv.Optional(CONF_RAW_LIGHTS_TEXT_SENSOR): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon="mdi:lightbulb-on-outline"
            ),
            cv.Optional(CONF_MSG_TYPES_TEXT_SENSOR): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon="mdi:message-text-outline"
            ),
            cv.Optional(CONF_RAW_OTHER_TEXT_SENSOR): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon="mdi:message-processing-outline"
            ),
            cv.Optional(CONF_TIME_TEXT_SENSOR): text_sensor.text_sensor_schema(icon="mdi:clock-outline"),
            cv.Optional(CONF_HEATER_MODE_TEXT_SENSOR): text_sensor.text_sensor_schema(icon="mdi:radiator"),
            cv.Optional(CONF_CLEARRAY_TEXT_SENSOR): text_sensor.text_sensor_schema(icon="mdi:lightbulb-fluorescent-tube"),
            cv.Optional(CONF_LANGUAGE_TEXT_SENSOR): text_sensor.text_sensor_schema(icon="mdi:translate"),
            cv.Optional(CONF_LOCK_TEXT_SENSOR): text_sensor.text_sensor_schema(icon="mdi:lock"),
            cv.Optional(CONF_TEMP_FORMAT_TEXT_SENSOR): text_sensor.text_sensor_schema(icon="mdi:thermometer"),
            cv.Optional(CONF_DATE_TEXT_SENSOR): text_sensor.text_sensor_schema(icon="mdi:calendar"),
            cv.Optional(CONF_CLOCK_SYNC_TEXT_SENSOR): text_sensor.text_sensor_schema(icon="mdi:clock-check-outline"),
            # "OK", or the last thing the decoder could not handle (cleared back to "OK" after an hour).
            cv.Optional(CONF_DECODER_NOTICE_TEXT_SENSOR): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon="mdi:alert-circle-outline"
            ),
            # Home Assistant time source for date disambiguation and the HA -> spa clock sync.
            cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            cv.Optional(CONF_CLOCK_AUTO_SYNC, default=True): cv.boolean,
            # Primary Filter settings (read from the PF menu nightly and whenever changed at the topside)
            cv.Optional(CONF_PF_START_TEXT_SENSOR): text_sensor.text_sensor_schema(icon="mdi:clock-start"),
            cv.Optional(CONF_PF_CYCLES_SENSOR): sensor.sensor_schema(accuracy_decimals=0, icon="mdi:counter"),
            cv.Optional(CONF_PF_DURATION_SENSOR): sensor.sensor_schema(
                unit_of_measurement="h", accuracy_decimals=0, icon="mdi:timer-outline"
            ),
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

    if CONF_PUMPS_RUNNING_SENSOR in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_PUMPS_RUNNING_SENSOR])
        cg.add(var.set_pumps_running_binary_sensor(sens))

    if CONF_CIRC_AUTO_SENSOR in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_CIRC_AUTO_SENSOR])
        cg.add(var.set_circulation_auto_binary_sensor(sens))

    if CONF_PUMP1_RUNNING_SENSOR in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_PUMP1_RUNNING_SENSOR])
        cg.add(var.set_pump1_running_binary_sensor(sens))

    if CONF_PUMP2_RUNNING_SENSOR in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_PUMP2_RUNNING_SENSOR])
        cg.add(var.set_pump2_running_binary_sensor(sens))

    if CONF_H2O_CHANGE_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_H2O_CHANGE_SENSOR])
        cg.add(var.set_h2o_change_sensor(sens))

    if CONF_FILTER_CHANGE_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_FILTER_CHANGE_SENSOR])
        cg.add(var.set_filter_change_sensor(sens))

    if CONF_UV_CHANGE_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_UV_CHANGE_SENSOR])
        cg.add(var.set_uv_change_sensor(sens))

    for conf in config.get(CONF_RAW_STATUS_SENSORS, []):
        sens = await sensor.new_sensor(conf)
        cg.add(var.add_raw_status_sensor(conf[CONF_FIELD], sens))

    for key, setter in (
        (CONF_JET1_RUN_SENSOR, var.set_jet1_run_sensor),
        (CONF_JET2_RUN_SENSOR, var.set_jet2_run_sensor),
        (CONF_LIGHT_RUN_SENSOR, var.set_light_run_sensor),
        (CONF_CIRC_RUN_SENSOR, var.set_circ_run_sensor),
        (CONF_HEATER_RUN_SENSOR, var.set_heater_run_sensor),
    ):
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(setter(sens))

    for conf in config.get(CONF_ACTIVITY, []):
        active = await binary_sensor.new_binary_sensor(conf[CONF_ACTIVE]) if CONF_ACTIVE in conf else cg.nullptr
        session = await binary_sensor.new_binary_sensor(conf[CONF_SESSION]) if CONF_SESSION in conf else cg.nullptr
        cg.add(var.add_activity(conf[CONF_ITEM], active, session, conf[CONF_SESSION_MIN].total_seconds))

    if CONF_SCREEN_KEY_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_SCREEN_KEY_SENSOR])
        cg.add(var.set_screen_key_sensor(sens))

    if CONF_PANEL_BUTTON_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_PANEL_BUTTON_SENSOR])
        cg.add(var.set_panel_button_sensor(sens))

    if CONF_RAW_LIGHTS_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_RAW_LIGHTS_TEXT_SENSOR])
        cg.add(var.set_raw_lights_text_sensor(sens))

    if CONF_MSG_TYPES_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_MSG_TYPES_TEXT_SENSOR])
        cg.add(var.set_msg_types_text_sensor(sens))

    if CONF_RAW_OTHER_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_RAW_OTHER_TEXT_SENSOR])
        cg.add(var.set_raw_other_text_sensor(sens))

    if CONF_HEATER_MODE_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_HEATER_MODE_TEXT_SENSOR])
        cg.add(var.set_heater_mode_text_sensor(sens))

    if CONF_CLEARRAY_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_CLEARRAY_TEXT_SENSOR])
        cg.add(var.set_clearray_text_sensor(sens))

    if CONF_LANGUAGE_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_LANGUAGE_TEXT_SENSOR])
        cg.add(var.set_language_text_sensor(sens))

    if CONF_LOCK_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_LOCK_TEXT_SENSOR])
        cg.add(var.set_lock_text_sensor(sens))

    if CONF_TEMP_FORMAT_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_TEMP_FORMAT_TEXT_SENSOR])
        cg.add(var.set_temp_format_text_sensor(sens))

    if CONF_DATE_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_DATE_TEXT_SENSOR])
        cg.add(var.set_date_text_sensor(sens))

    if CONF_CLOCK_SYNC_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_CLOCK_SYNC_TEXT_SENSOR])
        cg.add(var.set_clock_sync_text_sensor(sens))

    if CONF_DECODER_NOTICE_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_DECODER_NOTICE_TEXT_SENSOR])
        cg.add(var.set_decoder_notice_text_sensor(sens))

    if CONF_TIME_ID in config:
        t = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(t))
    cg.add(var.set_clock_auto_sync(config[CONF_CLOCK_AUTO_SYNC]))

    if CONF_PF_START_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_PF_START_TEXT_SENSOR])
        cg.add(var.set_pf_start_text_sensor(sens))
    if CONF_PF_CYCLES_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_PF_CYCLES_SENSOR])
        cg.add(var.set_pf_cycles_sensor(sens))
    if CONF_PF_DURATION_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_PF_DURATION_SENSOR])
        cg.add(var.set_pf_duration_sensor(sens))

    if CONF_TIME_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_TIME_TEXT_SENSOR])
        cg.add(var.set_time_text_sensor(sens))
