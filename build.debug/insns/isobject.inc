#define dst regbase[r0]
#define v v1

DEFLABEL(HEAD):
INSN_COUNT1(isobject,v);Lisobject0:
TLisobject_special:
TLisobject_any:
TLisobject_string:
TLisobject_iterator:
TLisobject_number_object:
TLisobject_regexp:
TLisobject_function:
TLisobject_string_object:
TLisobject_builtin:
TLisobject_simple_object:
TLisobject_flonum:
TLisobject_boolean_object:
TLisobject_array:
TLisobject_fixnum:
{ // normal_builtin vmgen.newsynth.DecisionDiagram$Leaf@54

/*
   isobject dst reg
     $dst = $reg is an Object or not
 */

  dst = true_false(is_object(v));
}
#undef dst
#undef v

