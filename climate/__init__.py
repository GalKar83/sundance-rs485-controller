import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from esphome.const import CONF_ID
from .. import CONF_SUNDANCE_SPA_ID, SundanceSpa, sundance_spa_ns

DEPENDENCIES = ["sundance_spa"]

SundanceClimate = sundance_spa_ns.class_("SundanceClimate", climate.Climate, cg.Component)

CONFIG_SCHEMA = (
    climate.climate_schema(SundanceClimate)
    .extend(
        {
            cv.GenerateID(CONF_SUNDANCE_SPA_ID): cv.use_id(SundanceSpa),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    parent = await cg.get_variable(config[CONF_SUNDANCE_SPA_ID])
    cg.add(var.set_parent(parent))
