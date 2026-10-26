import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG

from .. import CONF_aux_ir_ID, aux_ir_climate, Feature, aux_ir_ns

aux_irSwitch = aux_ir_ns.class_("aux_irSwitch", switch.Switch)

# config key -> (Feature, icon, entity category)
SWITCHES = {
    "turbo": ("TURBO", "mdi:fan-plus", None),
    "eco": ("ECO", "mdi:leaf", None),
    "sleep": ("SLEEP", "mdi:power-sleep", None),
    "health": ("HEALTH", "mdi:air-purifier", None),
    "fahrenheit": ("FAHRENHEIT", "mdi:temperature-fahrenheit", ENTITY_CATEGORY_CONFIG),
}


def _schema(icon, category):
    kwargs = {"icon": icon, "default_restore_mode": "RESTORE_DEFAULT_OFF"}
    if category is not None:
        kwargs["entity_category"] = category
    return switch.switch_schema(aux_irSwitch, **kwargs)


CONFIG_SCHEMA = cv.Schema(
    {cv.GenerateID(CONF_aux_ir_ID): cv.use_id(aux_ir_climate)}
    | {cv.Optional(key): _schema(icon, cat) for key, (_, icon, cat) in SWITCHES.items()}
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_aux_ir_ID])
    for key, (feature, _, _) in SWITCHES.items():
        if conf := config.get(key):
            feat = getattr(Feature, f"FEATURE_{feature}")
            sw = await switch.new_switch(conf, feat)
            await cg.register_parented(sw, parent)
            cg.add(parent.set_feature_switch(feat, sw))
