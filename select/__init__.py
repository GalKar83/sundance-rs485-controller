import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_ID, CONF_NAME, CONF_OPTIONS, CONF_ICON
from .. import CONF_SUNDANCE_SPA_ID, SundanceSpa, sundance_spa_ns

DEPENDENCIES = ["sundance_spa"]

SundanceLightsSelect = sundance_spa_ns.class_("SundanceLightsSelect", select.Select, cg.Component)
SundanceLightColorSelect = sundance_spa_ns.class_(
    "SundanceLightColorSelect", select.Select, cg.Component
)

CONF_TYPE = "type"

LIGHTS_OPTIONS = ["Off", "Low", "Medium", "High"]
# 680: one "Light" select — Off plus the 10 modes in the manual's button order.
LIGHT_COLOR_OPTIONS = [
    "Off",
    "High Speed Blend",
    "Low Speed Blend",
    "Frozen Blend",
    "Blue",
    "Violet",
    "Red",
    "Amber",
    "Green",
    "Aqua",
    "Near White",
]

BASE_SCHEMA = select._SELECT_SCHEMA.extend(
    {
        cv.GenerateID(CONF_SUNDANCE_SPA_ID): cv.use_id(SundanceSpa),
    }
).extend(cv.COMPONENT_SCHEMA)

LIGHTS_SCHEMA = BASE_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(SundanceLightsSelect),
        cv.Optional(CONF_NAME, default="Lights"): cv.string,
        cv.Optional(CONF_ICON, default="mdi:lightbulb"): cv.icon,
        cv.Optional(CONF_OPTIONS, default=LIGHTS_OPTIONS): cv.ensure_list(cv.string),
    }
)

LIGHT_COLOR_SCHEMA = BASE_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(SundanceLightColorSelect),
        cv.Optional(CONF_NAME, default="Light Color"): cv.string,
        cv.Optional(CONF_ICON, default="mdi:palette"): cv.icon,
        cv.Optional(CONF_OPTIONS, default=LIGHT_COLOR_OPTIONS): cv.ensure_list(cv.string),
    }
)

CONFIG_SCHEMA = cv.typed_schema(
    {
        "lights": LIGHTS_SCHEMA,
        "light_color": LIGHT_COLOR_SCHEMA,
    },
    lower=True,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_SUNDANCE_SPA_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await select.register_select(var, config, options=config[CONF_OPTIONS])
    cg.add(var.set_parent(parent))

    if config[CONF_TYPE] == "lights":
        cg.add(parent.set_lights_select(var))
    else:
        cg.add(parent.set_light_color_select(var))
