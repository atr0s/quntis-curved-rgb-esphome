import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch

from . import CONF_QUNTIS_RGB_ID, QuntisRgb, quntis_rgb_ns

DEPENDENCIES = ["quntis_rgb"]

QuntisAutoSwitch = quntis_rgb_ns.class_("QuntisAutoSwitch", switch.Switch)

CONFIG_SCHEMA = switch.switch_schema(QuntisAutoSwitch, icon="mdi:brightness-auto").extend(
    {cv.GenerateID(CONF_QUNTIS_RGB_ID): cv.use_id(QuntisRgb)}
)


async def to_code(config):
    var = await switch.new_switch(config)
    hub = await cg.get_variable(config[CONF_QUNTIS_RGB_ID])
    cg.add(var.set_parent(hub))
    cg.add(hub.set_auto_switch(var))
