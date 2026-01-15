#define v v0
#define d d1

DEFLABEL(HEAD):
INSN_COUNT1(jumpfalse,v);Ljumpfalse0:
TLjumpfalse_any:
TLjumpfalse_special:
TLjumpfalse_function:
TLjumpfalse_iterator:
TLjumpfalse_simple_object:
TLjumpfalse_string:
TLjumpfalse_string_object:
TLjumpfalse_number_object:
TLjumpfalse_regexp:
TLjumpfalse_flonum:
TLjumpfalse_fixnum:
TLjumpfalse_boolean_object:
TLjumpfalse_builtin:
TLjumpfalse_array:
{ // normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@54

/*
   jumpfalse src disp
   if (!$src) pc = pc + $disp
 */

  if (v == JS_FALSE || to_boolean(v) == JS_FALSE) {
    set_pc_relative(d);
    NEXT_INSN_NOINCPC();
  }
}
#undef v
#undef d

