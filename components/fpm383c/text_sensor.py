import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_TYPE

from . import CONF_FPM383C_ID, CONF_TYPE_LAST_MATCH, CONF_TYPE_STATUS, FPM383C

DEPENDENCIES = ["fpm383c"]

TYPES = {
    CONF_TYPE_STATUS: "set_status_sensor",
    CONF_TYPE_LAST_MATCH: "set_last_match_sensor",
}

CONFIG_SCHEMA = text_sensor.text_sensor_schema().extend(
    {
        cv.GenerateID(CONF_FPM383C_ID): cv.use_id(FPM383C),
        cv.Required(CONF_TYPE): cv.one_of(*TYPES.keys(), lower=True),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_FPM383C_ID])
    var = await text_sensor.new_text_sensor(config)
    setter = TYPES[config[CONF_TYPE]]
    cg.add(getattr(parent, setter)(var))
