#define dst regbase[r0]
#define v v1

DEFLABEL(HEAD):
INSN_COUNT1(isundef,v);Lisundef0:
TLisundef_fixnum:
TLisundef_boolean_object:
TLisundef_number_object:
TLisundef_array:
TLisundef_any:
TLisundef_regexp:
TLisundef_flonum:
TLisundef_simple_object:
TLisundef_function:
TLisundef_string_object:
TLisundef_string:
TLisundef_builtin:
TLisundef_iterator:
TLisundef_special:
{ // normal_builtin vmgen.newsynth.DecisionDiagram$Leaf@54

/*
   isundef dst reg
     $dst = $reg == undefined
 */

  dst = true_false(is_undefined(v));
}
#undef dst
#undef v

