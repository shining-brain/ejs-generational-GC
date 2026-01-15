#define src v0

DEFLABEL(HEAD):
INSN_COUNT1(throw,src);Lthrow0:
TLthrow_array:
TLthrow_special:
TLthrow_iterator:
TLthrow_number_object:
TLthrow_function:
TLthrow_regexp:
TLthrow_flonum:
TLthrow_string:
TLthrow_any:
TLthrow_boolean_object:
TLthrow_builtin:
TLthrow_fixnum:
TLthrow_simple_object:
TLthrow_string_object:
{ // normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@54

  save_context();
  set_a(context, src);
  exhandler_throw(context);
  /* exhandler_throw never returns */
}
#undef src

