import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = []
MULTI_CONF = True

CONF_QUNTIS_RGB_ID = "quntis_rgb_id"
CONF_CE_PIN = "ce_pin"
CONF_CS_PIN = "cs_pin"
CONF_SPI_CLK_PIN = "spi_clk_pin"
CONF_SPI_MOSI_PIN = "spi_mosi_pin"
CONF_SPI_MISO_PIN = "spi_miso_pin"
CONF_ADDRESS = "address"
CONF_REMOTE_ID = "remote_id"
CONF_CHANNELS = "channels"
CONF_RX_CHANNEL = "rx_channel"
CONF_TX_REPEATS = "tx_repeats"

quntis_rgb_ns = cg.esphome_ns.namespace("quntis_rgb")
QuntisRgb = quntis_rgb_ns.class_("QuntisRgb", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(QuntisRgb),
        cv.Optional(CONF_CE_PIN, default=1): cv.int_range(min=0, max=48),
        cv.Optional(CONF_CS_PIN, default=5): cv.int_range(min=0, max=48),
        cv.Optional(CONF_SPI_CLK_PIN, default=2): cv.int_range(min=0, max=48),
        cv.Optional(CONF_SPI_MOSI_PIN, default=4): cv.int_range(min=0, max=48),
        cv.Optional(CONF_SPI_MISO_PIN, default=3): cv.int_range(min=0, max=48),
        # XN297 address of the remote, as decoded (descrambled, MSB first)
        cv.Required(CONF_ADDRESS): cv.All(cv.ensure_list(cv.hex_uint8_t), cv.Length(min=5, max=5)),
        # First four payload bytes, identifying the remote
        cv.Required(CONF_REMOTE_ID): cv.All(cv.ensure_list(cv.hex_uint8_t), cv.Length(min=4, max=4)),
        cv.Optional(CONF_CHANNELS, default=[19, 53, 80]): cv.All(
            cv.ensure_list(cv.int_range(min=0, max=125)), cv.Length(min=1, max=8)
        ),
        cv.Optional(CONF_RX_CHANNEL, default=53): cv.int_range(min=0, max=125),
        cv.Optional(CONF_TX_REPEATS, default=40): cv.int_range(min=1, max=200),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    cg.add_library("SPI", None)
    cg.add_library("nrf24/RF24", "1.6.2")

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(
        var.set_pins(
            config[CONF_CE_PIN],
            config[CONF_CS_PIN],
            config[CONF_SPI_CLK_PIN],
            config[CONF_SPI_MOSI_PIN],
            config[CONF_SPI_MISO_PIN],
        )
    )
    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(var.set_remote_id(config[CONF_REMOTE_ID]))
    cg.add(var.set_channels(config[CONF_CHANNELS]))
    cg.add(var.set_rx_channel(config[CONF_RX_CHANNEL]))
    cg.add(var.set_tx_repeats(config[CONF_TX_REPEATS]))
