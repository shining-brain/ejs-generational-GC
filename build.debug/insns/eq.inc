#define dst regbase[r0]

DEFLABEL(HEAD):
INSN_COUNT2(eq,v1,v2);Leq0:
TLeq_string_object_any:
TLeq_flonum_any:
TLeq_any_any:
TLeq_iterator_any:
TLeq_string_any:
TLeq_fixnum_any:
TLeq_regexp_any:
TLeq_boolean_object_any:
TLeq_array_any:
TLeq_special_any:
TLeq_builtin_any:
TLeq_simple_object_any:
TLeq_function_any:
TLeq_number_object_any:
switch((unsigned int) get_ptag(v2).v){ // vmgen.newsynth.DecisionDiagram$PTNode@550(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_FIXNUM:
default:
case TV_GENERIC:
Leq1:
TLeq_regexp_simple_object:
TLeq_boolean_object_array:
TLeq_fixnum_regexp:
TLeq_regexp_function:
TLeq_boolean_object_regexp:
TLeq_array_number_object:
TLeq_array_array:
TLeq_any_array:
TLeq_special_string_object:
TLeq_string_array:
TLeq_boolean_object_iterator:
TLeq_function_builtin:
TLeq_simple_object_special:
TLeq_simple_object_flonum:
TLeq_string_object_special:
TLeq_string_object_string_object:
TLeq_fixnum_number_object:
TLeq_string_object_array:
TLeq_array_iterator:
TLeq_special_array:
TLeq_function_boolean_object:
TLeq_any_simple_object:
TLeq_string_regexp:
TLeq_function_fixnum:
TLeq_number_object_function:
TLeq_array_boolean_object:
TLeq_number_object_array:
TLeq_builtin_array:
TLeq_string_number_object:
TLeq_function_string:
TLeq_array_flonum:
TLeq_simple_object_array:
TLeq_flonum_iterator:
TLeq_array_builtin:
TLeq_builtin_simple_object:
TLeq_string_object_boolean_object:
TLeq_fixnum_string_object:
TLeq_special_boolean_object:
TLeq_builtin_function:
TLeq_flonum_fixnum:
TLeq_fixnum_function:
TLeq_flonum_string:
TLeq_array_string_object:
TLeq_iterator_string_object:
TLeq_boolean_object_string_object:
TLeq_any_string_object:
TLeq_builtin_builtin:
TLeq_regexp_special:
TLeq_regexp_number_object:
TLeq_regexp_string:
TLeq_string_object_flonum:
TLeq_function_array:
TLeq_function_iterator:
TLeq_regexp_builtin:
TLeq_simple_object_simple_object:
TLeq_special_fixnum:
TLeq_simple_object_boolean_object:
TLeq_any_fixnum:
TLeq_any_iterator:
TLeq_special_regexp:
TLeq_special_number_object:
TLeq_special_function:
TLeq_builtin_regexp:
TLeq_flonum_simple_object:
TLeq_iterator_iterator:
TLeq_array_regexp:
TLeq_boolean_object_boolean_object:
TLeq_array_fixnum:
TLeq_string_fixnum:
TLeq_flonum_number_object:
TLeq_special_simple_object:
TLeq_number_object_regexp:
TLeq_any_function:
TLeq_string_object_builtin:
TLeq_simple_object_string_object:
TLeq_fixnum_simple_object:
TLeq_builtin_fixnum:
TLeq_builtin_boolean_object:
TLeq_special_builtin:
TLeq_fixnum_iterator:
TLeq_iterator_string:
TLeq_any_regexp:
TLeq_function_flonum:
TLeq_simple_object_fixnum:
TLeq_string_object_string:
TLeq_regexp_flonum:
TLeq_number_object_fixnum:
TLeq_flonum_string_object:
TLeq_fixnum_fixnum:
TLeq_flonum_boolean_object:
TLeq_builtin_iterator:
TLeq_builtin_string_object:
TLeq_boolean_object_fixnum:
TLeq_fixnum_array:
TLeq_fixnum_flonum:
TLeq_flonum_function:
TLeq_boolean_object_function:
TLeq_boolean_object_string:
TLeq_simple_object_regexp:
TLeq_boolean_object_flonum:
TLeq_simple_object_number_object:
TLeq_array_function:
TLeq_iterator_boolean_object:
TLeq_flonum_builtin:
TLeq_any_builtin:
TLeq_fixnum_string:
TLeq_boolean_object_builtin:
TLeq_boolean_object_number_object:
TLeq_string_boolean_object:
TLeq_number_object_iterator:
TLeq_special_special:
TLeq_iterator_flonum:
TLeq_builtin_number_object:
TLeq_number_object_flonum:
TLeq_iterator_simple_object:
TLeq_number_object_boolean_object:
TLeq_boolean_object_simple_object:
TLeq_fixnum_builtin:
TLeq_any_boolean_object:
TLeq_string_object_iterator:
TLeq_number_object_string_object:
TLeq_string_flonum:
TLeq_function_function:
TLeq_regexp_iterator:
TLeq_special_flonum:
TLeq_iterator_function:
TLeq_fixnum_special:
TLeq_boolean_object_special:
TLeq_flonum_special:
TLeq_builtin_special:
TLeq_regexp_boolean_object:
TLeq_regexp_array:
TLeq_builtin_flonum:
TLeq_string_builtin:
TLeq_iterator_special:
TLeq_simple_object_iterator:
TLeq_regexp_fixnum:
TLeq_string_function:
TLeq_flonum_regexp:
TLeq_function_number_object:
TLeq_array_special:
TLeq_string_object_regexp:
TLeq_iterator_builtin:
TLeq_string_special:
TLeq_string_object_number_object:
TLeq_number_object_number_object:
TLeq_simple_object_function:
TLeq_function_string_object:
TLeq_array_simple_object:
TLeq_regexp_regexp:
TLeq_any_string:
TLeq_string_string_object:
TLeq_special_iterator:
TLeq_string_string:
TLeq_function_regexp:
TLeq_regexp_string_object:
TLeq_number_object_special:
TLeq_iterator_regexp:
TLeq_special_string:
TLeq_string_iterator:
TLeq_function_special:
TLeq_iterator_number_object:
TLeq_fixnum_boolean_object:
TLeq_simple_object_builtin:
TLeq_string_object_function:
TLeq_number_object_simple_object:
TLeq_string_object_simple_object:
TLeq_number_object_builtin:
TLeq_number_object_string:
TLeq_builtin_string:
TLeq_string_object_fixnum:
TLeq_string_simple_object:
TLeq_flonum_array:
TLeq_function_simple_object:
TLeq_array_string:
TLeq_any_special:
TLeq_iterator_array:
TLeq_iterator_fixnum:
TLeq_simple_object_string:
TLeq_any_number_object:
{ // normal_simple_object normal_iterator vmgen.newsynth.DecisionDiagram$Leaf@4e

  if (v1 == v2) dst = false_true(is_nan(v1));
  else dst = JS_FALSE;
}
break;
case TV_FLONUM:
Leq2:
TLeq_any_flonum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@173c(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_FIXNUM:
default:
case TV_GENERIC:
goto Leq1;
break;
case TV_FLONUM:
Leq3:
TLeq_flonum_flonum:
{ // normal_flonum normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@dbb

  double x1 = flonum_to_double(v1);
  double x2 = flonum_to_double(v2);
  dst = true_false(x1 == x2);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@173c
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@550
#undef dst

