import esphome.codegen as cg
from esphome.components import climate_ir
import esphome.config_validation as cv

from . import aux_ir_climate

DEPENDENCIES = ["remote_transmitter"]
AUTO_LOAD = ["climate_ir"]

CONF_USE_FAHRENHEIT = "use_fahrenheit"

CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(aux_ir_climate).extend(
    {
        cv.Optional(CONF_USE_FAHRENHEIT, default=False): cv.boolean,
    }
)


async def to_code(config):
    var = await climate_ir.new_climate_ir(config)
    cg.add(var.set_use_fahrenheit(config[CONF_USE_FAHRENHEIT]))
