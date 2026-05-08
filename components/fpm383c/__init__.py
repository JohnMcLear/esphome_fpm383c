import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = ["@johnmclear"]
DEPENDENCIES = ["uart"]
MULTI_CONF = True

fpm383c_ns = cg.esphome_ns.namespace("fpm383c")
FPM383C = fpm383c_ns.class_("FPM383C", cg.Component, uart.UARTDevice)

CONF_FPM383C_ID = "fpm383c_id"
CONF_POLL_INTERVAL = "poll_interval"

CONF_TYPE_SLOT = "slot"
CONF_TYPE_STATUS = "status"
CONF_TYPE_LAST_MATCH = "last_match"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(FPM383C),
            cv.Optional(CONF_POLL_INTERVAL, default="500ms"): cv.positive_time_period_milliseconds,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
