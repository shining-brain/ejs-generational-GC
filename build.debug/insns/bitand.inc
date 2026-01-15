#define dst regbase[r0]

DEFLABEL(HEAD):
INSN_COUNT2(bitand,v1,v2);Lbitand0:
TLbitand_simple_object_any:
TLbitand_any_any:
TLbitand_string_object_any:
TLbitand_builtin_any:
TLbitand_array_any:
TLbitand_boolean_object_any:
TLbitand_fixnum_any:
TLbitand_string_any:
TLbitand_regexp_any:
TLbitand_number_object_any:
TLbitand_iterator_any:
TLbitand_flonum_any:
TLbitand_special_any:
TLbitand_function_any:
switch((unsigned int) get_ptag(v2).v){ // vmgen.newsynth.DecisionDiagram$PTNode@550(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lbitand1:
TLbitand_any_array:
TLbitand_any_builtin:
TLbitand_any_regexp:
TLbitand_any_simple_object:
TLbitand_any_iterator:
TLbitand_any_number_object:
TLbitand_any_string:
TLbitand_any_function:
TLbitand_any_boolean_object:
TLbitand_any_special:
TLbitand_any_string_object:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@5f3(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lbitand2:
TLbitand_iterator_string_object:
TLbitand_function_array:
TLbitand_builtin_special:
TLbitand_iterator_string:
TLbitand_string_builtin:
TLbitand_special_function:
TLbitand_boolean_object_boolean_object:
TLbitand_number_object_builtin:
TLbitand_simple_object_iterator:
TLbitand_boolean_object_string_object:
TLbitand_string_simple_object:
TLbitand_iterator_boolean_object:
TLbitand_array_array:
TLbitand_number_object_special:
TLbitand_boolean_object_number_object:
TLbitand_function_number_object:
TLbitand_function_regexp:
TLbitand_special_string:
TLbitand_builtin_simple_object:
TLbitand_simple_object_string:
TLbitand_builtin_regexp:
TLbitand_function_string_object:
TLbitand_special_array:
TLbitand_simple_object_string_object:
TLbitand_special_string_object:
TLbitand_iterator_builtin:
TLbitand_iterator_number_object:
TLbitand_regexp_simple_object:
TLbitand_string_regexp:
TLbitand_string_object_simple_object:
TLbitand_boolean_object_regexp:
TLbitand_function_special:
TLbitand_boolean_object_function:
TLbitand_builtin_iterator:
TLbitand_iterator_special:
TLbitand_string_array:
TLbitand_simple_object_special:
TLbitand_string_object_special:
TLbitand_function_builtin:
TLbitand_array_iterator:
TLbitand_number_object_string:
TLbitand_special_number_object:
TLbitand_array_string_object:
TLbitand_function_iterator:
TLbitand_number_object_iterator:
TLbitand_string_object_builtin:
TLbitand_array_special:
TLbitand_simple_object_boolean_object:
TLbitand_string_special:
TLbitand_array_simple_object:
TLbitand_simple_object_function:
TLbitand_array_function:
TLbitand_string_object_number_object:
TLbitand_string_string_object:
TLbitand_string_object_boolean_object:
TLbitand_string_function:
TLbitand_regexp_iterator:
TLbitand_array_boolean_object:
TLbitand_regexp_string:
TLbitand_iterator_iterator:
TLbitand_builtin_array:
TLbitand_boolean_object_simple_object:
TLbitand_iterator_simple_object:
TLbitand_array_string:
TLbitand_array_builtin:
TLbitand_number_object_boolean_object:
TLbitand_iterator_regexp:
TLbitand_simple_object_simple_object:
TLbitand_function_boolean_object:
TLbitand_regexp_string_object:
TLbitand_boolean_object_iterator:
TLbitand_special_regexp:
TLbitand_regexp_builtin:
TLbitand_regexp_regexp:
TLbitand_number_object_number_object:
TLbitand_array_number_object:
TLbitand_regexp_special:
TLbitand_boolean_object_string:
TLbitand_special_simple_object:
TLbitand_string_object_array:
TLbitand_simple_object_regexp:
TLbitand_string_object_regexp:
TLbitand_number_object_array:
TLbitand_string_object_function:
TLbitand_string_boolean_object:
TLbitand_builtin_string:
TLbitand_regexp_boolean_object:
TLbitand_regexp_array:
TLbitand_string_iterator:
TLbitand_regexp_function:
TLbitand_simple_object_array:
TLbitand_builtin_number_object:
TLbitand_number_object_simple_object:
TLbitand_string_string:
TLbitand_iterator_function:
TLbitand_function_simple_object:
TLbitand_builtin_boolean_object:
TLbitand_special_builtin:
TLbitand_iterator_array:
TLbitand_special_special:
TLbitand_function_function:
TLbitand_number_object_regexp:
TLbitand_string_number_object:
TLbitand_special_boolean_object:
TLbitand_simple_object_builtin:
TLbitand_boolean_object_builtin:
TLbitand_string_object_iterator:
TLbitand_number_object_string_object:
TLbitand_string_object_string_object:
TLbitand_boolean_object_array:
TLbitand_number_object_function:
TLbitand_string_object_string:
TLbitand_simple_object_number_object:
TLbitand_array_regexp:
TLbitand_regexp_number_object:
TLbitand_special_iterator:
TLbitand_builtin_string_object:
TLbitand_builtin_function:
TLbitand_builtin_builtin:
TLbitand_function_string:
TLbitand_boolean_object_special:
{ // normal_array normal_special vmgen.newsynth.DecisionDiagram$Leaf@16

  v1 = to_number(context, v1);
  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
case TV_FLONUM:
case TV_FIXNUM:
default:
Lbitand3:
TLbitand_fixnum_special:
TLbitand_fixnum_simple_object:
TLbitand_flonum_special:
TLbitand_fixnum_iterator:
TLbitand_flonum_simple_object:
TLbitand_fixnum_string_object:
TLbitand_flonum_regexp:
TLbitand_flonum_number_object:
TLbitand_fixnum_function:
TLbitand_flonum_iterator:
TLbitand_flonum_builtin:
TLbitand_fixnum_regexp:
TLbitand_fixnum_string:
TLbitand_fixnum_builtin:
TLbitand_flonum_string:
TLbitand_flonum_string_object:
TLbitand_flonum_boolean_object:
TLbitand_fixnum_array:
TLbitand_flonum_function:
TLbitand_fixnum_number_object:
TLbitand_fixnum_boolean_object:
TLbitand_flonum_array:
{ // normal_flonum normal_special vmgen.newsynth.DecisionDiagram$Leaf@54

  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@5f3
break;
case TV_FLONUM:
Lbitand4:
TLbitand_any_flonum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@e6b(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lbitand5:
TLbitand_array_fixnum:
TLbitand_builtin_flonum:
TLbitand_special_flonum:
TLbitand_simple_object_fixnum:
TLbitand_string_flonum:
TLbitand_simple_object_flonum:
TLbitand_string_object_fixnum:
TLbitand_builtin_fixnum:
TLbitand_string_fixnum:
TLbitand_string_object_flonum:
TLbitand_array_flonum:
TLbitand_iterator_fixnum:
TLbitand_boolean_object_flonum:
TLbitand_number_object_flonum:
TLbitand_function_fixnum:
TLbitand_regexp_fixnum:
TLbitand_boolean_object_fixnum:
TLbitand_iterator_flonum:
TLbitand_function_flonum:
TLbitand_special_fixnum:
TLbitand_regexp_flonum:
TLbitand_number_object_fixnum:
{ // normal_builtin normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@f3

  v1 = to_number(context, v1);
  goto USELABEL(HEAD);
}
break;
case TV_FIXNUM:
default:
Lbitand6:
TLbitand_fixnum_flonum:
{ // normal_fixnum normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@840

  int32_t x1 = (int32_t) fixnum_to_cint(v1);
  int32_t x2 = (int32_t) flonum_to_cint(v2);
  dst = small_cint_to_fixnum((cint) (x1 & x2));
}
break;
case TV_FLONUM:
Lbitand7:
TLbitand_flonum_flonum:
{ // normal_flonum normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@a53

  int32_t x1 = (int32_t) flonum_to_cint(v1);
  int32_t x2 = (int32_t) flonum_to_cint(v2);
  dst = int32_to_number(context, x1 & x2);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@e6b
break;
case TV_FIXNUM:
default:
Lbitand8:
TLbitand_any_fixnum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@1f02(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
goto Lbitand5;
break;
case TV_FLONUM:
Lbitand9:
TLbitand_flonum_fixnum:
{ // normal_flonum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@4ae

  int32_t x1 = (int32_t) flonum_to_cint(v1);
  int32_t x2 = (int32_t) fixnum_to_cint(v2);
  dst = small_cint_to_fixnum((cint) (x1 & x2));
}
break;
case TV_FIXNUM:
default:
Lbitand10:
TLbitand_fixnum_fixnum:
{ // normal_fixnum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@947

  dst = v1 & v2;
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@1f02
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@550
#undef dst

