import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_MAX_VALUE, CONF_MIN_VALUE, CONF_STEP, CONF_TYPE

from . import CONF_FPM383C_ID, CONF_TYPE_SLOT, FPM383C, fpm383c_ns

DEPENDENCIES = ["fpm383c"]

FPM383CNumber = fpm383c_ns.class_("FPM383CNumber", number.Number, cg.Component)

CONFIG_SCHEMA = number.number_schema(FPM383CNumber).extend(
    {
        cv.GenerateID(CONF_FPM383C_ID): cv.use_id(FPM383C),
        cv.Required(CONF_TYPE): cv.one_of(CONF_TYPE_SLOT, lower=True),
        cv.Optional(CONF_MIN_VALUE, default=1): cv.int_range(min=1, max=64),
        cv.Optional(CONF_MAX_VALUE, default=64): cv.int_range(min=1, max=64),
        cv.Optional(CONF_STEP, default=1): cv.positive_int,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_FPM383C_ID])
    var = await number.new_number(
        config,
        min_value=config[CONF_MIN_VALUE],
        max_value=config[CONF_MAX_VALUE],
        step=config[CONF_STEP],
    )
    await cg.register_component(var, config)
    if config[CONF_TYPE] == CONF_TYPE_SLOT:
        cg.add(parent.set_slot_number(var))
