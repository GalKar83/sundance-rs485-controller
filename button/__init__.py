import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from .. import CONF_SUNDANCE_SPA_ID, SundanceSpa, sundance_spa_ns

DEPENDENCIES = ["sundance_spa"]

SundanceButton = sundance_spa_ns.class_("SundanceButton", button.Button, cg.Component)

CONF_BUTTON_DATAPOINT = "button_datapoint"

CONFIG_SCHEMA = (
    button.button_schema(SundanceButton)
    .extend(
        {
            cv.GenerateID(CONF_SUNDANCE_SPA_ID): cv.use_id(SundanceSpa),
            # 3 = Circulation (ClearRay). Jets use the switch platform; lights use select.
            cv.Required(CONF_BUTTON_DATAPOINT): cv.int_range(min=3, max=3),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_SUNDANCE_SPA_ID])
    var = await button.new_button(config)
    await cg.register_component(var, config)
    cg.add(var.set_parent(parent))
    cg.add(var.set_button_id(config[CONF_BUTTON_DATAPOINT]))
