#define dst regbase[r0]

DEFLABEL(HEAD):
INSN_COUNT2(div,v1,v2);Ldiv0:
TLdiv_boolean_object_any:
TLdiv_regexp_any:
TLdiv_builtin_any:
TLdiv_string_object_any:
TLdiv_string_any:
TLdiv_iterator_any:
TLdiv_fixnum_any:
TLdiv_function_any:
TLdiv_simple_object_any:
TLdiv_array_any:
TLdiv_number_object_any:
TLdiv_special_any:
TLdiv_any_any:
TLdiv_flonum_any:
switch((unsigned int) get_ptag(v2).v){ // vmgen.newsynth.DecisionDiagram$PTNode@550(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Ldiv1:
TLdiv_any_iterator:
TLdiv_any_simple_object:
TLdiv_any_string_object:
TLdiv_any_string:
TLdiv_any_builtin:
TLdiv_any_boolean_object:
TLdiv_any_array:
TLdiv_any_regexp:
TLdiv_any_function:
TLdiv_any_special:
TLdiv_any_number_object:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@17c(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Ldiv2:
TLdiv_builtin_builtin:
TLdiv_string_object_number_object:
TLdiv_string_function:
TLdiv_boolean_object_number_object:
TLdiv_array_number_object:
TLdiv_array_iterator:
TLdiv_string_object_array:
TLdiv_simple_object_array:
TLdiv_iterator_string:
TLdiv_number_object_boolean_object:
TLdiv_builtin_boolean_object:
TLdiv_function_regexp:
TLdiv_string_simple_object:
TLdiv_string_object_string_object:
TLdiv_number_object_function:
TLdiv_special_array:
TLdiv_simple_object_function:
TLdiv_special_regexp:
TLdiv_builtin_function:
TLdiv_regexp_string:
TLdiv_number_object_string:
TLdiv_builtin_special:
TLdiv_regexp_array:
TLdiv_number_object_array:
TLdiv_function_builtin:
TLdiv_string_object_string:
TLdiv_iterator_iterator:
TLdiv_string_object_function:
TLdiv_simple_object_string:
TLdiv_function_boolean_object:
TLdiv_regexp_iterator:
TLdiv_boolean_object_array:
TLdiv_iterator_special:
TLdiv_regexp_special:
TLdiv_special_string_object:
TLdiv_number_object_iterator:
TLdiv_boolean_object_string:
TLdiv_function_array:
TLdiv_string_object_simple_object:
TLdiv_special_builtin:
TLdiv_string_object_special:
TLdiv_iterator_builtin:
TLdiv_string_object_builtin:
TLdiv_simple_object_regexp:
TLdiv_special_iterator:
TLdiv_builtin_regexp:
TLdiv_number_object_simple_object:
TLdiv_array_boolean_object:
TLdiv_boolean_object_simple_object:
TLdiv_array_regexp:
TLdiv_iterator_simple_object:
TLdiv_string_regexp:
TLdiv_function_special:
TLdiv_boolean_object_iterator:
TLdiv_special_number_object:
TLdiv_special_simple_object:
TLdiv_regexp_builtin:
TLdiv_iterator_string_object:
TLdiv_simple_object_boolean_object:
TLdiv_iterator_boolean_object:
TLdiv_boolean_object_string_object:
TLdiv_regexp_function:
TLdiv_builtin_iterator:
TLdiv_function_number_object:
TLdiv_number_object_number_object:
TLdiv_string_object_boolean_object:
TLdiv_string_string_object:
TLdiv_regexp_regexp:
TLdiv_string_array:
TLdiv_iterator_array:
TLdiv_simple_object_iterator:
TLdiv_array_function:
TLdiv_builtin_simple_object:
TLdiv_simple_object_simple_object:
TLdiv_regexp_number_object:
TLdiv_string_object_regexp:
TLdiv_number_object_special:
TLdiv_array_special:
TLdiv_special_special:
TLdiv_function_string_object:
TLdiv_number_object_string_object:
TLdiv_function_string:
TLdiv_number_object_regexp:
TLdiv_builtin_number_object:
TLdiv_regexp_string_object:
TLdiv_boolean_object_function:
TLdiv_boolean_object_regexp:
TLdiv_array_builtin:
TLdiv_iterator_number_object:
TLdiv_builtin_array:
TLdiv_simple_object_number_object:
TLdiv_regexp_boolean_object:
TLdiv_function_iterator:
TLdiv_array_simple_object:
TLdiv_iterator_function:
TLdiv_string_builtin:
TLdiv_builtin_string:
TLdiv_boolean_object_builtin:
TLdiv_boolean_object_boolean_object:
TLdiv_special_function:
TLdiv_regexp_simple_object:
TLdiv_simple_object_special:
TLdiv_simple_object_builtin:
TLdiv_string_special:
TLdiv_boolean_object_special:
TLdiv_string_boolean_object:
TLdiv_special_string:
TLdiv_function_simple_object:
TLdiv_special_boolean_object:
TLdiv_string_object_iterator:
TLdiv_array_array:
TLdiv_array_string_object:
TLdiv_string_string:
TLdiv_string_iterator:
TLdiv_function_function:
TLdiv_string_number_object:
TLdiv_simple_object_string_object:
TLdiv_builtin_string_object:
TLdiv_array_string:
TLdiv_iterator_regexp:
TLdiv_number_object_builtin:
{ // normal_builtin normal_regexp vmgen.newsynth.DecisionDiagram$Leaf@16

  v1 = to_number(context, v1);
  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
case TV_FLONUM:
case TV_FIXNUM:
default:
Ldiv3:
TLdiv_flonum_regexp:
TLdiv_flonum_simple_object:
TLdiv_flonum_string_object:
TLdiv_flonum_builtin:
TLdiv_flonum_boolean_object:
TLdiv_flonum_number_object:
TLdiv_flonum_special:
TLdiv_fixnum_function:
TLdiv_fixnum_boolean_object:
TLdiv_fixnum_string:
TLdiv_fixnum_array:
TLdiv_fixnum_iterator:
TLdiv_flonum_string:
TLdiv_fixnum_simple_object:
TLdiv_fixnum_string_object:
TLdiv_flonum_array:
TLdiv_fixnum_number_object:
TLdiv_fixnum_regexp:
TLdiv_fixnum_builtin:
TLdiv_fixnum_special:
TLdiv_flonum_iterator:
TLdiv_flonum_function:
{ // normal_fixnum normal_string vmgen.newsynth.DecisionDiagram$Leaf@222

  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@17c
break;
case TV_FLONUM:
Ldiv4:
TLdiv_any_flonum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@173c(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Ldiv5:
TLdiv_string_fixnum:
TLdiv_number_object_flonum:
TLdiv_iterator_flonum:
TLdiv_iterator_fixnum:
TLdiv_string_flonum:
TLdiv_special_flonum:
TLdiv_function_fixnum:
TLdiv_array_fixnum:
TLdiv_regexp_flonum:
TLdiv_builtin_fixnum:
TLdiv_boolean_object_fixnum:
TLdiv_special_fixnum:
TLdiv_string_object_fixnum:
TLdiv_simple_object_fixnum:
TLdiv_string_object_flonum:
TLdiv_builtin_flonum:
TLdiv_number_object_fixnum:
TLdiv_simple_object_flonum:
TLdiv_function_flonum:
TLdiv_boolean_object_flonum:
TLdiv_regexp_fixnum:
TLdiv_array_flonum:
{ // normal_regexp normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@df

  v1 = to_number(context, v1);
  goto USELABEL(HEAD);
}
break;
case TV_FLONUM:
case TV_FIXNUM:
default:
Ldiv6:
TLdiv_fixnum_flonum:
TLdiv_flonum_fixnum:
TLdiv_flonum_flonum:
{ // normal_flonum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@13e8

  double x1 = to_double(context, v1);
  double x2 = to_double(context, v2);
  double d = x1 / x2;
  if (isinf(d)) dst = d > 0 ? gconsts.g_flonum_infinity : gconsts.g_flonum_negative_infinity;
  else if (isnan(d)) dst = gconsts.g_flonum_nan;
  else dst = double_to_number(context, d);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@173c
break;
case TV_FIXNUM:
default:
Ldiv7:
TLdiv_any_fixnum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@220f(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
goto Ldiv5;
break;
case TV_FIXNUM:
default:
Ldiv8:
TLdiv_fixnum_fixnum:
{ // normal_fixnum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@a53

  cint n1, n2, s;
  n1 = fixnum_to_cint(v1);
  if (v2 == FIXNUM_ZERO) {
    if (n1 > 0) dst = gconsts.g_flonum_infinity;
    else if (n1 == 0) dst = gconsts.g_flonum_nan;
    else dst = gconsts.g_flonum_negative_infinity;
  } else {
    n2 = fixnum_to_cint(v2);
    s = n1/ n2;
    dst = ((n1 == n2 * s) ? cint_to_number(context, s) :
           double_to_flonum(context, (double) n1 / (double) n2));
  }
}
break;
case TV_FLONUM:
goto Ldiv6;
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@220f
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@550
#undef dst

