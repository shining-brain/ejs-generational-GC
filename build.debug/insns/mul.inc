#define dst regbase[r0]

DEFLABEL(HEAD):
INSN_COUNT2(mul,v1,v2);Lmul0:
TLmul_boolean_object_any:
TLmul_function_any:
TLmul_flonum_any:
TLmul_array_any:
TLmul_special_any:
TLmul_string_object_any:
TLmul_iterator_any:
TLmul_regexp_any:
TLmul_builtin_any:
TLmul_simple_object_any:
TLmul_any_any:
TLmul_number_object_any:
TLmul_fixnum_any:
TLmul_string_any:
switch((unsigned int) get_ptag(v2).v){ // vmgen.newsynth.DecisionDiagram$PTNode@550(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lmul1:
TLmul_any_iterator:
TLmul_any_string_object:
TLmul_any_function:
TLmul_any_regexp:
TLmul_any_simple_object:
TLmul_any_builtin:
TLmul_any_boolean_object:
TLmul_any_array:
TLmul_any_number_object:
TLmul_any_special:
TLmul_any_string:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@17c(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lmul2:
TLmul_special_simple_object:
TLmul_simple_object_boolean_object:
TLmul_string_function:
TLmul_builtin_builtin:
TLmul_string_string_object:
TLmul_boolean_object_special:
TLmul_number_object_number_object:
TLmul_function_boolean_object:
TLmul_array_iterator:
TLmul_special_iterator:
TLmul_iterator_function:
TLmul_iterator_string_object:
TLmul_simple_object_iterator:
TLmul_special_array:
TLmul_number_object_iterator:
TLmul_boolean_object_iterator:
TLmul_builtin_regexp:
TLmul_builtin_special:
TLmul_regexp_number_object:
TLmul_builtin_string_object:
TLmul_boolean_object_number_object:
TLmul_function_number_object:
TLmul_string_object_iterator:
TLmul_simple_object_simple_object:
TLmul_function_array:
TLmul_array_string:
TLmul_boolean_object_array:
TLmul_builtin_number_object:
TLmul_function_string_object:
TLmul_regexp_special:
TLmul_number_object_array:
TLmul_simple_object_function:
TLmul_function_builtin:
TLmul_number_object_string_object:
TLmul_string_object_string_object:
TLmul_builtin_iterator:
TLmul_string_object_builtin:
TLmul_string_object_simple_object:
TLmul_regexp_iterator:
TLmul_simple_object_number_object:
TLmul_regexp_builtin:
TLmul_iterator_simple_object:
TLmul_regexp_array:
TLmul_string_regexp:
TLmul_regexp_regexp:
TLmul_string_object_special:
TLmul_array_number_object:
TLmul_special_string_object:
TLmul_function_simple_object:
TLmul_iterator_builtin:
TLmul_array_array:
TLmul_function_regexp:
TLmul_function_iterator:
TLmul_string_object_string:
TLmul_number_object_simple_object:
TLmul_function_special:
TLmul_simple_object_regexp:
TLmul_boolean_object_regexp:
TLmul_iterator_iterator:
TLmul_number_object_regexp:
TLmul_string_object_function:
TLmul_iterator_number_object:
TLmul_iterator_array:
TLmul_iterator_boolean_object:
TLmul_function_function:
TLmul_boolean_object_function:
TLmul_iterator_special:
TLmul_boolean_object_simple_object:
TLmul_string_array:
TLmul_builtin_boolean_object:
TLmul_number_object_special:
TLmul_iterator_regexp:
TLmul_boolean_object_boolean_object:
TLmul_string_iterator:
TLmul_builtin_simple_object:
TLmul_array_function:
TLmul_array_simple_object:
TLmul_string_string:
TLmul_special_string:
TLmul_special_boolean_object:
TLmul_string_object_number_object:
TLmul_number_object_builtin:
TLmul_builtin_string:
TLmul_regexp_simple_object:
TLmul_regexp_function:
TLmul_array_string_object:
TLmul_builtin_function:
TLmul_special_number_object:
TLmul_array_regexp:
TLmul_array_boolean_object:
TLmul_simple_object_builtin:
TLmul_string_object_array:
TLmul_number_object_boolean_object:
TLmul_number_object_string:
TLmul_simple_object_array:
TLmul_string_builtin:
TLmul_simple_object_string_object:
TLmul_special_regexp:
TLmul_regexp_string_object:
TLmul_special_special:
TLmul_simple_object_special:
TLmul_function_string:
TLmul_array_special:
TLmul_string_number_object:
TLmul_boolean_object_string:
TLmul_regexp_boolean_object:
TLmul_string_boolean_object:
TLmul_string_special:
TLmul_string_simple_object:
TLmul_regexp_string:
TLmul_string_object_boolean_object:
TLmul_simple_object_string:
TLmul_boolean_object_builtin:
TLmul_builtin_array:
TLmul_string_object_regexp:
TLmul_iterator_string:
TLmul_number_object_function:
TLmul_boolean_object_string_object:
TLmul_special_builtin:
TLmul_array_builtin:
TLmul_special_function:
{ // normal_builtin normal_regexp vmgen.newsynth.DecisionDiagram$Leaf@16

  v1 = to_number(context, v1);
  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
case TV_FLONUM:
case TV_FIXNUM:
default:
Lmul3:
TLmul_flonum_special:
TLmul_flonum_array:
TLmul_flonum_string_object:
TLmul_flonum_iterator:
TLmul_flonum_boolean_object:
TLmul_flonum_simple_object:
TLmul_flonum_regexp:
TLmul_fixnum_function:
TLmul_fixnum_special:
TLmul_fixnum_array:
TLmul_fixnum_iterator:
TLmul_flonum_builtin:
TLmul_fixnum_builtin:
TLmul_fixnum_regexp:
TLmul_fixnum_number_object:
TLmul_fixnum_string:
TLmul_fixnum_boolean_object:
TLmul_flonum_function:
TLmul_fixnum_string_object:
TLmul_fixnum_simple_object:
TLmul_flonum_number_object:
TLmul_flonum_string:
{ // normal_fixnum normal_string vmgen.newsynth.DecisionDiagram$Leaf@222

  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@17c
break;
case TV_FLONUM:
Lmul4:
TLmul_any_flonum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@173c(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lmul5:
TLmul_iterator_flonum:
TLmul_builtin_flonum:
TLmul_string_flonum:
TLmul_simple_object_flonum:
TLmul_function_fixnum:
TLmul_builtin_fixnum:
TLmul_string_fixnum:
TLmul_iterator_fixnum:
TLmul_boolean_object_fixnum:
TLmul_special_fixnum:
TLmul_number_object_fixnum:
TLmul_array_flonum:
TLmul_boolean_object_flonum:
TLmul_regexp_fixnum:
TLmul_string_object_fixnum:
TLmul_simple_object_fixnum:
TLmul_function_flonum:
TLmul_string_object_flonum:
TLmul_array_fixnum:
TLmul_special_flonum:
TLmul_number_object_flonum:
TLmul_regexp_flonum:
{ // normal_regexp normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@df

  v1 = to_number(context, v1);
  goto USELABEL(HEAD);
}
break;
case TV_FLONUM:
case TV_FIXNUM:
default:
Lmul6:
TLmul_flonum_flonum:
TLmul_flonum_fixnum:
TLmul_fixnum_flonum:
{ // normal_flonum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@13e8

  double x1 = to_double(context, v1);
  double x2 = to_double(context, v2);
  dst = double_to_number(context, x1 * x2);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@173c
break;
case TV_FIXNUM:
default:
Lmul7:
TLmul_any_fixnum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@220f(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
goto Lmul5;
break;
case TV_FIXNUM:
default:
Lmul8:
TLmul_fixnum_fixnum:
{ // normal_fixnum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@a53

  cint n1, n2;
  n1 = fixnum_to_cint(v1);
  n2 = fixnum_to_cint(v2);
  if (half_fixnum_range(n1) && half_fixnum_range(n2)) {
    cint p = n1 * n2;
    dst = small_cint_to_fixnum(p);
  } else {
    double p = (double) n1 * (double) n2;
    dst = double_to_number(context, p);
  }
}
break;
case TV_FLONUM:
goto Lmul6;
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@220f
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@550
#undef dst

