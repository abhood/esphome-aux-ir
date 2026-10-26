import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv

from .. import CONF_aux_ir_ID, aux_ir_climate, ButtonType, aux_ir_ns

aux_irButton = aux_ir_ns.class_("aux_irButton", button.Button)

BUTTONS = {
    "display": ("DISPLAY", "mdi:led-on"),
    "iclean": ("ICLEAN", "mdi:spray-bottle"),
}

CONFIG_SCHEMA = cv.Schema(
    {cv.GenerateID(CONF_aux_ir_ID): cv.use_id(aux_ir_climate)}
    | {cv.Optional(key): button.button_schema(aux_irButton, icon=icon) for key, (_, icon) in BUTTONS.items()}
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_aux_ir_ID])
    for key, (kind, _) in BUTTONS.items():
        if conf := config.get(key):
            btn = await button.new_button(conf, getattr(ButtonType, f"BUTTON_{kind}"))
            await cg.register_parented(btn, parent)
