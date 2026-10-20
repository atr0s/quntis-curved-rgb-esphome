import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import (
    CONF_COLD_WHITE_COLOR_TEMPERATURE,
    CONF_DEFAULT_TRANSITION_LENGTH,
    CONF_GAMMA_CORRECT,
    CONF_OUTPUT_ID,
    CONF_WARM_WHITE_COLOR_TEMPERATURE,
)

from . import CONF_QUNTIS_RGB_ID, QuntisRgb, quntis_rgb_ns

DEPENDENCIES = ["quntis_rgb"]

QuntisRgbLight = quntis_rgb_ns.class_("QuntisRgbLight", light.LightOutput)

CONFIG_SCHEMA = light.LIGHT_SCHEMA.extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(QuntisRgbLight),
        cv.GenerateID(CONF_QUNTIS_RGB_ID): cv.use_id(QuntisRgb),
        cv.Optional(CONF_DEFAULT_TRANSITION_LENGTH, default="0s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_GAMMA_CORRECT, default=1.0): cv.positive_float,
        cv.Optional(CONF_COLD_WHITE_COLOR_TEMPERATURE, default="6500K"): cv.color_temperature,
        cv.Optional(CONF_WARM_WHITE_COLOR_TEMPERATURE, default="3000K"): cv.color_temperature,
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)
    hub = await cg.get_variable(config[CONF_QUNTIS_RGB_ID])
    cg.add(var.set_parent(hub))
    cg.add(var.set_mireds(config[CONF_COLD_WHITE_COLOR_TEMPERATURE], config[CONF_WARM_WHITE_COLOR_TEMPERATURE]))
    cg.add(hub.set_light(var))
