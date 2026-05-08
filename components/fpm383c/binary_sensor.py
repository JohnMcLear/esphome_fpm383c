import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_TYPE

from . import CONF_FPM383C_ID, FPM383C

DEPENDENCIES = ["fpm383c"]

TYPE_FINGER = "finger"

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(
    device_class="occupancy",
).extend(
    {
        cv.GenerateID(CONF_FPM383C_ID): cv.use_id(FPM383C),
        cv.Required(CONF_TYPE): cv.one_of(TYPE_FINGER, lower=True),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_FPM383C_ID])
    var = await binary_sensor.new_binary_sensor(config)
    if config[CONF_TYPE] == TYPE_FINGER:
        cg.add(parent.set_finger_sensor(var))
