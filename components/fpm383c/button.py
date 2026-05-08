import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_TYPE

from . import CONF_FPM383C_ID, FPM383C, fpm383c_ns

DEPENDENCIES = ["fpm383c"]

FPM383CButton = fpm383c_ns.class_("FPM383CButton", button.Button)
ButtonAction = fpm383c_ns.enum("ButtonAction", is_class=True)

TYPES = {
    "enroll":     ButtonAction.ENROLL,
    "cancel":     ButtonAction.CANCEL,
    "delete":     ButtonAction.DELETE_ONE,
    "delete_all": ButtonAction.DELETE_ALL,
}

CONFIG_SCHEMA = button.button_schema(FPM383CButton).extend(
    {
        cv.GenerateID(CONF_FPM383C_ID): cv.use_id(FPM383C),
        cv.Required(CONF_TYPE): cv.one_of(*TYPES.keys(), lower=True),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_FPM383C_ID])
    var = await button.new_button(config)
    cg.add(var.set_parent(parent))
    cg.add(var.set_action(TYPES[config[CONF_TYPE]]))
