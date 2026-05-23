#define v v0
#define d d1

DEFLABEL(HEAD):
INSN_COUNT1(jumptrue,v);Ljumptrue0:
TLjumptrue_string:
TLjumptrue_number_object:
TLjumptrue_array:
TLjumptrue_regexp:
TLjumptrue_builtin:
TLjumptrue_simple_object:
TLjumptrue_string_object:
TLjumptrue_flonum:
TLjumptrue_any:
TLjumptrue_boolean_object:
TLjumptrue_function:
TLjumptrue_special:
TLjumptrue_fixnum:
TLjumptrue_iterator:
{ // normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@54

/*
   jumptrue src disp
   if (!$src) pc = pc + $disp
 */

  if (v == JS_TRUE || to_boolean(v) == JS_TRUE) {
    set_pc_relative(d);
    NEXT_INSN_NOINCPC();
  }
}
#undef v
#undef d

