import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import (
    CONF_COLD_WHITE_COLOR_TEMPERATURE,
    CONF_DEFAULT_TRANSITION_LENGTH,
    CONF_GAMMA_CORRECT,
    CONF_OUTPUT_ID,
    CONF_TYPE,
    CONF_WARM_WHITE_COLOR_TEMPERATURE,
)

from . import CONF_QUNTIS_RGB_ID, QuntisRgb, quntis_rgb_ns

DEPENDENCIES = ["quntis_rgb"]

QuntisRgbLight = quntis_rgb_ns.class_("QuntisRgbLight", light.LightOutput)
QuntisBacklightLight = quntis_rgb_ns.class_("QuntisBacklightLight", light.LightOutput)

TYPE_MAIN = "main"
TYPE_BACKLIGHT = "backlight"

MAIN_SCHEMA = light.LIGHT_SCHEMA.extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(QuntisRgbLight),
        cv.GenerateID(CONF_QUNTIS_RGB_ID): cv.use_id(QuntisRgb),
        cv.Optional(CONF_DEFAULT_TRANSITION_LENGTH, default="0s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_GAMMA_CORRECT, default=1.0): cv.positive_float,
        cv.Optional(CONF_COLD_WHITE_COLOR_TEMPERATURE, default="6500K"): cv.color_temperature,
        cv.Optional(CONF_WARM_WHITE_COLOR_TEMPERATURE, default="3000K"): cv.color_temperature,
    }
)

# On/off for the RGB backlight; turning it on brings back the last mode
BACKLIGHT_SCHEMA = light.BINARY_LIGHT_SCHEMA.extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(QuntisBacklightLight),
        cv.GenerateID(CONF_QUNTIS_RGB_ID): cv.use_id(QuntisRgb),
    }
)

CONFIG_SCHEMA = cv.typed_schema(
    {TYPE_MAIN: MAIN_SCHEMA, TYPE_BACKLIGHT: BACKLIGHT_SCHEMA}, default_type=TYPE_MAIN
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)
    hub = await cg.get_variable(config[CONF_QUNTIS_RGB_ID])
    cg.add(var.set_parent(hub))
    if config[CONF_TYPE] == TYPE_BACKLIGHT:
        cg.add(hub.set_backlight_light(var))
        return
    cg.add(var.set_mireds(config[CONF_COLD_WHITE_COLOR_TEMPERATURE], config[CONF_WARM_WHITE_COLOR_TEMPERATURE]))
    cg.add(hub.set_light(var))
