#define dst regbase[r0]

DEFLABEL(HEAD):
INSN_COUNT2(leftshift,v1,v2);Lleftshift0:
TLleftshift_function_any:
TLleftshift_any_any:
TLleftshift_simple_object_any:
TLleftshift_string_any:
TLleftshift_fixnum_any:
TLleftshift_number_object_any:
TLleftshift_regexp_any:
TLleftshift_iterator_any:
TLleftshift_builtin_any:
TLleftshift_string_object_any:
TLleftshift_boolean_object_any:
TLleftshift_array_any:
TLleftshift_flonum_any:
TLleftshift_special_any:
switch((unsigned int) get_ptag(v2).v){ // vmgen.newsynth.DecisionDiagram$PTNode@550(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lleftshift1:
TLleftshift_any_array:
TLleftshift_any_string:
TLleftshift_any_number_object:
TLleftshift_any_function:
TLleftshift_any_boolean_object:
TLleftshift_any_builtin:
TLleftshift_any_simple_object:
TLleftshift_any_string_object:
TLleftshift_any_iterator:
TLleftshift_any_regexp:
TLleftshift_any_special:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@5f3(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lleftshift2:
TLleftshift_function_special:
TLleftshift_number_object_builtin:
TLleftshift_number_object_string_object:
TLleftshift_number_object_iterator:
TLleftshift_boolean_object_builtin:
TLleftshift_simple_object_number_object:
TLleftshift_simple_object_special:
TLleftshift_function_builtin:
TLleftshift_regexp_function:
TLleftshift_string_array:
TLleftshift_special_string_object:
TLleftshift_iterator_builtin:
TLleftshift_string_object_special:
TLleftshift_array_string_object:
TLleftshift_boolean_object_regexp:
TLleftshift_number_object_number_object:
TLleftshift_boolean_object_string_object:
TLleftshift_simple_object_boolean_object:
TLleftshift_builtin_regexp:
TLleftshift_regexp_string:
TLleftshift_simple_object_string_object:
TLleftshift_string_simple_object:
TLleftshift_string_object_string:
TLleftshift_function_simple_object:
TLleftshift_special_regexp:
TLleftshift_builtin_function:
TLleftshift_string_object_simple_object:
TLleftshift_simple_object_string:
TLleftshift_boolean_object_array:
TLleftshift_builtin_simple_object:
TLleftshift_array_string:
TLleftshift_regexp_special:
TLleftshift_array_number_object:
TLleftshift_number_object_regexp:
TLleftshift_special_array:
TLleftshift_iterator_boolean_object:
TLleftshift_function_array:
TLleftshift_boolean_object_number_object:
TLleftshift_special_number_object:
TLleftshift_iterator_number_object:
TLleftshift_string_function:
TLleftshift_number_object_boolean_object:
TLleftshift_builtin_number_object:
TLleftshift_iterator_regexp:
TLleftshift_array_boolean_object:
TLleftshift_string_object_function:
TLleftshift_string_object_boolean_object:
TLleftshift_string_object_number_object:
TLleftshift_boolean_object_boolean_object:
TLleftshift_builtin_iterator:
TLleftshift_iterator_special:
TLleftshift_simple_object_function:
TLleftshift_function_string_object:
TLleftshift_function_boolean_object:
TLleftshift_regexp_builtin:
TLleftshift_special_simple_object:
TLleftshift_function_regexp:
TLleftshift_iterator_simple_object:
TLleftshift_boolean_object_function:
TLleftshift_number_object_array:
TLleftshift_string_string:
TLleftshift_string_object_builtin:
TLleftshift_special_builtin:
TLleftshift_string_object_string_object:
TLleftshift_builtin_array:
TLleftshift_array_special:
TLleftshift_string_boolean_object:
TLleftshift_boolean_object_simple_object:
TLleftshift_special_special:
TLleftshift_regexp_string_object:
TLleftshift_string_string_object:
TLleftshift_regexp_boolean_object:
TLleftshift_builtin_string_object:
TLleftshift_string_iterator:
TLleftshift_iterator_array:
TLleftshift_special_iterator:
TLleftshift_regexp_regexp:
TLleftshift_builtin_boolean_object:
TLleftshift_boolean_object_iterator:
TLleftshift_simple_object_regexp:
TLleftshift_regexp_iterator:
TLleftshift_regexp_array:
TLleftshift_special_string:
TLleftshift_number_object_simple_object:
TLleftshift_iterator_string_object:
TLleftshift_simple_object_iterator:
TLleftshift_array_function:
TLleftshift_boolean_object_string:
TLleftshift_array_array:
TLleftshift_iterator_iterator:
TLleftshift_array_builtin:
TLleftshift_array_simple_object:
TLleftshift_number_object_string:
TLleftshift_string_regexp:
TLleftshift_function_number_object:
TLleftshift_iterator_string:
TLleftshift_string_builtin:
TLleftshift_string_number_object:
TLleftshift_builtin_special:
TLleftshift_builtin_builtin:
TLleftshift_function_string:
TLleftshift_simple_object_simple_object:
TLleftshift_builtin_string:
TLleftshift_special_function:
TLleftshift_function_iterator:
TLleftshift_string_object_iterator:
TLleftshift_string_object_regexp:
TLleftshift_special_boolean_object:
TLleftshift_array_regexp:
TLleftshift_regexp_number_object:
TLleftshift_simple_object_builtin:
TLleftshift_simple_object_array:
TLleftshift_function_function:
TLleftshift_regexp_simple_object:
TLleftshift_boolean_object_special:
TLleftshift_number_object_function:
TLleftshift_string_special:
TLleftshift_string_object_array:
TLleftshift_iterator_function:
TLleftshift_array_iterator:
TLleftshift_number_object_special:
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
Lleftshift3:
TLleftshift_flonum_array:
TLleftshift_flonum_regexp:
TLleftshift_fixnum_iterator:
TLleftshift_flonum_number_object:
TLleftshift_fixnum_simple_object:
TLleftshift_fixnum_regexp:
TLleftshift_flonum_string_object:
TLleftshift_fixnum_builtin:
TLleftshift_flonum_simple_object:
TLleftshift_fixnum_string_object:
TLleftshift_fixnum_function:
TLleftshift_fixnum_array:
TLleftshift_fixnum_string:
TLleftshift_flonum_iterator:
TLleftshift_flonum_boolean_object:
TLleftshift_fixnum_special:
TLleftshift_flonum_builtin:
TLleftshift_flonum_special:
TLleftshift_fixnum_boolean_object:
TLleftshift_flonum_function:
TLleftshift_flonum_string:
TLleftshift_fixnum_number_object:
{ // normal_flonum normal_special vmgen.newsynth.DecisionDiagram$Leaf@54

  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@5f3
break;
case TV_FLONUM:
Lleftshift4:
TLleftshift_any_flonum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@e6b(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lleftshift5:
TLleftshift_boolean_object_flonum:
TLleftshift_simple_object_fixnum:
TLleftshift_string_object_flonum:
TLleftshift_string_fixnum:
TLleftshift_simple_object_flonum:
TLleftshift_number_object_fixnum:
TLleftshift_function_flonum:
TLleftshift_array_fixnum:
TLleftshift_regexp_fixnum:
TLleftshift_number_object_flonum:
TLleftshift_boolean_object_fixnum:
TLleftshift_iterator_fixnum:
TLleftshift_string_object_fixnum:
TLleftshift_builtin_fixnum:
TLleftshift_special_fixnum:
TLleftshift_iterator_flonum:
TLleftshift_function_fixnum:
TLleftshift_special_flonum:
TLleftshift_string_flonum:
TLleftshift_builtin_flonum:
TLleftshift_regexp_flonum:
TLleftshift_array_flonum:
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
Lleftshift6:
TLleftshift_fixnum_flonum:
{ // normal_fixnum normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@840

  int32_t x1 = (int32_t) fixnum_to_cint(v1);
  cint x2 = flonum_to_cint(v2) & 31;
  dst = cint_to_number(context, (cint) (x1 << x2));
}
break;
case TV_FLONUM:
Lleftshift7:
TLleftshift_flonum_flonum:
{ // normal_flonum normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@a53

  int32_t x1 = (int32_t) flonum_to_cint(v1);
  cint x2 = flonum_to_cint(v2) & 31;
  dst = cint_to_number(context, (cint) (x1 << x2));
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@e6b
break;
case TV_FIXNUM:
default:
Lleftshift8:
TLleftshift_any_fixnum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@1f02(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
goto Lleftshift5;
break;
case TV_FLONUM:
Lleftshift9:
TLleftshift_flonum_fixnum:
{ // normal_flonum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@4ae

  int32_t x1 = (int32_t) flonum_to_cint(v1);
  cint x2 = fixnum_to_cint(v2) & 31;
  dst = cint_to_number(context, (cint) (x1 << x2));
}
break;
case TV_FIXNUM:
default:
Lleftshift10:
TLleftshift_fixnum_fixnum:
{ // normal_fixnum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@947

  int32_t x1 = (int32_t) fixnum_to_cint(v1);
  cint x2 = fixnum_to_cint(v2) & 31;
  dst = cint_to_number(context, (cint) (x1 << x2));
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@1f02
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@550
#undef dst

