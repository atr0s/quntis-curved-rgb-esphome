import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select

from . import CONF_QUNTIS_RGB_ID, QuntisRgb, quntis_rgb_ns

DEPENDENCIES = ["quntis_rgb"]

QuntisBacklightSelect = quntis_rgb_ns.class_("QuntisBacklightSelect", select.Select)

CONF_MODES = "modes"

# Names for the 15 backlight modes, in remote order (mode 1 first)
CONFIG_SCHEMA = select.select_schema(QuntisBacklightSelect, icon="mdi:led-strip-variant").extend(
    {
        cv.GenerateID(CONF_QUNTIS_RGB_ID): cv.use_id(QuntisRgb),
        cv.Optional(CONF_MODES, default=[f"Mode {i}" for i in range(1, 16)]): cv.All(
            cv.ensure_list(cv.string_strict), cv.Length(min=15, max=15)
        ),
    }
)


async def to_code(config):
    var = await select.new_select(config, options=["Off"] + config[CONF_MODES])
    hub = await cg.get_variable(config[CONF_QUNTIS_RGB_ID])
    cg.add(var.set_parent(hub))
    cg.add(hub.set_backlight_select(var))
