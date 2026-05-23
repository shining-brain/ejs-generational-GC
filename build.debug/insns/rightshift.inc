#define dst regbase[r0]

DEFLABEL(HEAD):
INSN_COUNT2(rightshift,v1,v2);Lrightshift0:
TLrightshift_flonum_any:
TLrightshift_array_any:
TLrightshift_any_any:
TLrightshift_fixnum_any:
TLrightshift_string_any:
TLrightshift_builtin_any:
TLrightshift_number_object_any:
TLrightshift_boolean_object_any:
TLrightshift_simple_object_any:
TLrightshift_regexp_any:
TLrightshift_function_any:
TLrightshift_string_object_any:
TLrightshift_special_any:
TLrightshift_iterator_any:
switch((unsigned int) get_ptag(v2).v){ // vmgen.newsynth.DecisionDiagram$PTNode@550(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lrightshift1:
TLrightshift_any_array:
TLrightshift_any_boolean_object:
TLrightshift_any_simple_object:
TLrightshift_any_builtin:
TLrightshift_any_function:
TLrightshift_any_string_object:
TLrightshift_any_special:
TLrightshift_any_string:
TLrightshift_any_regexp:
TLrightshift_any_iterator:
TLrightshift_any_number_object:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@5f3(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lrightshift2:
TLrightshift_string_simple_object:
TLrightshift_simple_object_special:
TLrightshift_special_special:
TLrightshift_boolean_object_regexp:
TLrightshift_simple_object_string:
TLrightshift_boolean_object_special:
TLrightshift_boolean_object_simple_object:
TLrightshift_string_object_iterator:
TLrightshift_array_number_object:
TLrightshift_builtin_string:
TLrightshift_function_function:
TLrightshift_special_iterator:
TLrightshift_iterator_number_object:
TLrightshift_special_builtin:
TLrightshift_special_string_object:
TLrightshift_builtin_array:
TLrightshift_string_object_special:
TLrightshift_array_builtin:
TLrightshift_iterator_simple_object:
TLrightshift_string_object_array:
TLrightshift_boolean_object_boolean_object:
TLrightshift_string_object_number_object:
TLrightshift_array_regexp:
TLrightshift_boolean_object_builtin:
TLrightshift_string_object_boolean_object:
TLrightshift_array_function:
TLrightshift_simple_object_function:
TLrightshift_regexp_simple_object:
TLrightshift_regexp_regexp:
TLrightshift_special_regexp:
TLrightshift_builtin_simple_object:
TLrightshift_number_object_array:
TLrightshift_function_string:
TLrightshift_builtin_iterator:
TLrightshift_boolean_object_number_object:
TLrightshift_simple_object_iterator:
TLrightshift_iterator_iterator:
TLrightshift_regexp_iterator:
TLrightshift_string_object_string:
TLrightshift_number_object_boolean_object:
TLrightshift_number_object_simple_object:
TLrightshift_iterator_string:
TLrightshift_string_object_simple_object:
TLrightshift_function_string_object:
TLrightshift_simple_object_array:
TLrightshift_number_object_regexp:
TLrightshift_function_builtin:
TLrightshift_array_string_object:
TLrightshift_function_number_object:
TLrightshift_string_string:
TLrightshift_string_object_function:
TLrightshift_iterator_array:
TLrightshift_regexp_number_object:
TLrightshift_regexp_function:
TLrightshift_function_boolean_object:
TLrightshift_array_iterator:
TLrightshift_iterator_function:
TLrightshift_number_object_special:
TLrightshift_boolean_object_array:
TLrightshift_string_builtin:
TLrightshift_number_object_builtin:
TLrightshift_string_object_string_object:
TLrightshift_function_simple_object:
TLrightshift_regexp_array:
TLrightshift_number_object_string_object:
TLrightshift_array_special:
TLrightshift_simple_object_number_object:
TLrightshift_array_simple_object:
TLrightshift_simple_object_builtin:
TLrightshift_regexp_string:
TLrightshift_simple_object_boolean_object:
TLrightshift_builtin_string_object:
TLrightshift_number_object_function:
TLrightshift_regexp_boolean_object:
TLrightshift_regexp_string_object:
TLrightshift_builtin_boolean_object:
TLrightshift_string_boolean_object:
TLrightshift_array_array:
TLrightshift_string_special:
TLrightshift_iterator_boolean_object:
TLrightshift_boolean_object_function:
TLrightshift_special_function:
TLrightshift_simple_object_regexp:
TLrightshift_special_boolean_object:
TLrightshift_builtin_number_object:
TLrightshift_iterator_builtin:
TLrightshift_builtin_builtin:
TLrightshift_function_iterator:
TLrightshift_function_special:
TLrightshift_array_string:
TLrightshift_simple_object_simple_object:
TLrightshift_boolean_object_string_object:
TLrightshift_special_string:
TLrightshift_iterator_special:
TLrightshift_number_object_number_object:
TLrightshift_builtin_regexp:
TLrightshift_string_function:
TLrightshift_special_array:
TLrightshift_boolean_object_string:
TLrightshift_function_regexp:
TLrightshift_iterator_string_object:
TLrightshift_string_number_object:
TLrightshift_simple_object_string_object:
TLrightshift_iterator_regexp:
TLrightshift_special_number_object:
TLrightshift_builtin_special:
TLrightshift_string_object_regexp:
TLrightshift_boolean_object_iterator:
TLrightshift_string_string_object:
TLrightshift_function_array:
TLrightshift_special_simple_object:
TLrightshift_regexp_builtin:
TLrightshift_array_boolean_object:
TLrightshift_regexp_special:
TLrightshift_number_object_iterator:
TLrightshift_string_object_builtin:
TLrightshift_number_object_string:
TLrightshift_string_regexp:
TLrightshift_string_array:
TLrightshift_builtin_function:
TLrightshift_string_iterator:
{ // normal_array normal_special vmgen.newsynth.DecisionDiagram$Leaf@16

  v1 = to_number(context, v1);
  if (v1 == gconsts.g_flonum_infinity ||
      v1 == gconsts.g_flonum_negative_infinity ||
      v1 == gconsts.g_flonum_nan) {
    v1 = FIXNUM_ZERO;
  }
  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
case TV_FLONUM:
case TV_FIXNUM:
default:
Lrightshift3:
TLrightshift_fixnum_special:
TLrightshift_flonum_iterator:
TLrightshift_flonum_boolean_object:
TLrightshift_flonum_array:
TLrightshift_fixnum_string:
TLrightshift_fixnum_function:
TLrightshift_flonum_function:
TLrightshift_flonum_simple_object:
TLrightshift_fixnum_regexp:
TLrightshift_flonum_string_object:
TLrightshift_flonum_builtin:
TLrightshift_fixnum_builtin:
TLrightshift_flonum_regexp:
TLrightshift_fixnum_iterator:
TLrightshift_fixnum_string_object:
TLrightshift_flonum_number_object:
TLrightshift_flonum_string:
TLrightshift_fixnum_number_object:
TLrightshift_fixnum_simple_object:
TLrightshift_fixnum_boolean_object:
TLrightshift_flonum_special:
TLrightshift_fixnum_array:
{ // normal_flonum normal_special vmgen.newsynth.DecisionDiagram$Leaf@54

  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@5f3
break;
case TV_FLONUM:
Lrightshift4:
TLrightshift_any_flonum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@e6b(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lrightshift5:
TLrightshift_special_flonum:
TLrightshift_iterator_flonum:
TLrightshift_function_fixnum:
TLrightshift_builtin_flonum:
TLrightshift_number_object_flonum:
TLrightshift_special_fixnum:
TLrightshift_array_flonum:
TLrightshift_function_flonum:
TLrightshift_number_object_fixnum:
TLrightshift_string_flonum:
TLrightshift_regexp_fixnum:
TLrightshift_string_object_fixnum:
TLrightshift_builtin_fixnum:
TLrightshift_boolean_object_flonum:
TLrightshift_regexp_flonum:
TLrightshift_simple_object_fixnum:
TLrightshift_string_fixnum:
TLrightshift_boolean_object_fixnum:
TLrightshift_iterator_fixnum:
TLrightshift_simple_object_flonum:
TLrightshift_array_fixnum:
TLrightshift_string_object_flonum:
{ // normal_builtin normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@f3

  v1 = to_number(context, v1);
  if (v1 == gconsts.g_flonum_infinity ||
      v1 == gconsts.g_flonum_negative_infinity ||
      v1 == gconsts.g_flonum_nan) {
    v1 = FIXNUM_ZERO;
  }
  goto USELABEL(HEAD);
}
break;
case TV_FIXNUM:
default:
Lrightshift6:
TLrightshift_fixnum_flonum:
{ // normal_fixnum normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@840

  int32_t x1 = (int32_t) fixnum_to_cint(v1);
  cint x2 = flonum_to_cint(v2) & 31;
  dst = small_cint_to_fixnum((cint) (x1 >> x2));
}
break;
case TV_FLONUM:
Lrightshift7:
TLrightshift_flonum_flonum:
{ // normal_flonum normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@a53

  int32_t x1 = (int32_t) flonum_to_cint(v1);
  cint x2 = flonum_to_cint(v2) & 31;
  dst = int32_to_number(context, x1 >> x2);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@e6b
break;
case TV_FIXNUM:
default:
Lrightshift8:
TLrightshift_any_fixnum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@1f02(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
goto Lrightshift5;
break;
case TV_FLONUM:
Lrightshift9:
TLrightshift_flonum_fixnum:
{ // normal_flonum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@4ae

  int32_t x1 = (int32_t) flonum_to_cint(v1);
  cint x2 = fixnum_to_cint(v2) & 31;
  dst = int32_to_number(context, x1 >> x2);
}
break;
case TV_FIXNUM:
default:
Lrightshift10:
TLrightshift_fixnum_fixnum:
{ // normal_fixnum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@947

  int32_t x1 = (int32_t) fixnum_to_cint(v1);
  cint x2 = fixnum_to_cint(v2) & 31;
  dst = small_cint_to_fixnum((cint) (x1 >> x2));
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@1f02
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@550
#undef dst

