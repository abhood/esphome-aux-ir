import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv

from .. import CONF_aux_ir_ID, aux_ir_climate, aux_ir_ns

aux_irTimerNumber = aux_ir_ns.class_("aux_irTimerNumber", number.Number)

CONF_TIMER = "timer"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_aux_ir_ID): cv.use_id(aux_ir_climate),
        cv.Optional(CONF_TIMER): number.number_schema(
            aux_irTimerNumber, icon="mdi:timer-outline", unit_of_measurement="h"
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_aux_ir_ID])
    if conf := config.get(CONF_TIMER):
        # 0 = cancel; 0.5 h steps below 10 h, whole hours from 10 to 24 (snapped in C++)
        num = await number.new_number(conf, min_value=0, max_value=24, step=0.5)
        await cg.register_parented(num, parent)
        cg.add(parent.set_timer_number(num))
