"""AUX 13-byte IR climate (YKR-H/101E-style remote): native ESPHome component."""

import esphome.codegen as cg
from esphome.components import climate_ir

CODEOWNERS = []

CONF_aux_ir_ID = "aux_ir_id"

aux_ir_ns = cg.esphome_ns.namespace("aux_ir")
aux_ir_climate = aux_ir_ns.class_("aux_ir_climate", climate_ir.ClimateIR)
Feature = aux_ir_ns.enum("Feature", is_class=True)
ButtonType = aux_ir_ns.enum("ButtonType", is_class=True)
