#define link i0
#define index s1
#define src v2

DEFLABEL(HEAD):
INSN_COUNT1(setarg,src);Lsetarg0:
TLsetarg_special:
TLsetarg_string:
TLsetarg_any:
TLsetarg_array:
TLsetarg_regexp:
TLsetarg_fixnum:
TLsetarg_simple_object:
TLsetarg_builtin:
TLsetarg_string_object:
TLsetarg_number_object:
TLsetarg_flonum:
TLsetarg_function:
TLsetarg_iterator:
TLsetarg_boolean_object:
{ // normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@54

  FunctionFrame *fr;
  JSValue arguments;
  int i;

  fr = get_lp(context);
  for (i = 0; i < link; i++) fr = fframe_prev(fr);
  // TODO: optimize
  arguments = fframe_arguments(fr);
  set_array_prop(context, arguments,
                 cint_to_number(context, (cint) index), src);
}
#undef link
#undef index
#undef src

