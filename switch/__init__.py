import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID

from .. import sundance_spa_ns, SundanceSpa, CONF_SUNDANCE_SPA_ID

DEPENDENCIES = ["sundance_spa"]

SundanceSwitch = sundance_spa_ns.class_("SundanceSwitch", switch.Switch, cg.Component)

CONF_SWITCH_DATAPOINT = "switch_datapoint"

CONFIG_SCHEMA = (
    switch.switch_schema(SundanceSwitch)
    .extend(
        {
            cv.GenerateID(): cv.declare_id(SundanceSwitch),
            cv.Required(CONF_SUNDANCE_SPA_ID): cv.use_id(SundanceSpa),
            # 1=Jet1 (680: HIGH), 2=Jet2, 3=Circulation (680: manual Jet 1 LOW, no UV).
            # 1 and 3 are mutually exclusive modes of the same pump.
            cv.Required(CONF_SWITCH_DATAPOINT): cv.int_range(min=1, max=3),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await switch.register_switch(var, config)

    parent = await cg.get_variable(config[CONF_SUNDANCE_SPA_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_switch_id(config[CONF_SWITCH_DATAPOINT]))
