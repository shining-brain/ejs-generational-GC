#define link i0
#define index s1
#define src v2

DEFLABEL(HEAD):
INSN_COUNT1(setlocal,src);Lsetlocal0:
TLsetlocal_special:
TLsetlocal_string:
TLsetlocal_regexp:
TLsetlocal_any:
TLsetlocal_string_object:
TLsetlocal_simple_object:
TLsetlocal_flonum:
TLsetlocal_builtin:
TLsetlocal_number_object:
TLsetlocal_fixnum:
TLsetlocal_array:
TLsetlocal_iterator:
TLsetlocal_boolean_object:
TLsetlocal_function:
{ // normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@54

  FunctionFrame *fr;
  int i;
  fr = get_lp(context);
  for (i = 0; i < link; i++) fr = fframe_prev(fr);
  fframe_locals_idx(fr, index) = src;
}
#undef link
#undef index
#undef src

