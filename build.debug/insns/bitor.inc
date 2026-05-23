#define dst regbase[r0]

DEFLABEL(HEAD):
INSN_COUNT2(bitor,v1,v2);Lbitor0:
TLbitor_string_object_any:
TLbitor_any_any:
TLbitor_number_object_any:
TLbitor_function_any:
TLbitor_simple_object_any:
TLbitor_fixnum_any:
TLbitor_special_any:
TLbitor_string_any:
TLbitor_regexp_any:
TLbitor_iterator_any:
TLbitor_flonum_any:
TLbitor_array_any:
TLbitor_boolean_object_any:
TLbitor_builtin_any:
switch((unsigned int) get_ptag(v2).v){ // vmgen.newsynth.DecisionDiagram$PTNode@550(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lbitor1:
TLbitor_any_number_object:
TLbitor_any_boolean_object:
TLbitor_any_builtin:
TLbitor_any_special:
TLbitor_any_array:
TLbitor_any_function:
TLbitor_any_regexp:
TLbitor_any_simple_object:
TLbitor_any_string:
TLbitor_any_iterator:
TLbitor_any_string_object:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@5f3(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lbitor2:
TLbitor_iterator_iterator:
TLbitor_special_string:
TLbitor_string_object_string:
TLbitor_boolean_object_string_object:
TLbitor_function_builtin:
TLbitor_string_object_function:
TLbitor_string_object_string_object:
TLbitor_simple_object_boolean_object:
TLbitor_function_regexp:
TLbitor_builtin_boolean_object:
TLbitor_special_special:
TLbitor_iterator_boolean_object:
TLbitor_regexp_array:
TLbitor_builtin_simple_object:
TLbitor_string_special:
TLbitor_string_builtin:
TLbitor_regexp_string:
TLbitor_number_object_string:
TLbitor_function_array:
TLbitor_simple_object_function:
TLbitor_regexp_iterator:
TLbitor_builtin_function:
TLbitor_builtin_special:
TLbitor_number_object_function:
TLbitor_string_object_number_object:
TLbitor_array_string_object:
TLbitor_boolean_object_iterator:
TLbitor_string_function:
TLbitor_function_boolean_object:
TLbitor_special_function:
TLbitor_simple_object_string:
TLbitor_array_string:
TLbitor_iterator_simple_object:
TLbitor_string_boolean_object:
TLbitor_builtin_string_object:
TLbitor_number_object_array:
TLbitor_number_object_builtin:
TLbitor_regexp_string_object:
TLbitor_string_string:
TLbitor_iterator_array:
TLbitor_boolean_object_string:
TLbitor_string_simple_object:
TLbitor_simple_object_string_object:
TLbitor_simple_object_builtin:
TLbitor_array_simple_object:
TLbitor_iterator_string_object:
TLbitor_builtin_string:
TLbitor_iterator_regexp:
TLbitor_array_number_object:
TLbitor_regexp_boolean_object:
TLbitor_number_object_special:
TLbitor_array_special:
TLbitor_boolean_object_function:
TLbitor_string_iterator:
TLbitor_string_number_object:
TLbitor_regexp_simple_object:
TLbitor_number_object_iterator:
TLbitor_simple_object_special:
TLbitor_string_object_regexp:
TLbitor_builtin_iterator:
TLbitor_regexp_function:
TLbitor_special_regexp:
TLbitor_function_number_object:
TLbitor_regexp_number_object:
TLbitor_array_builtin:
TLbitor_iterator_number_object:
TLbitor_builtin_array:
TLbitor_regexp_builtin:
TLbitor_simple_object_number_object:
TLbitor_number_object_regexp:
TLbitor_number_object_string_object:
TLbitor_iterator_function:
TLbitor_boolean_object_special:
TLbitor_function_iterator:
TLbitor_special_array:
TLbitor_regexp_special:
TLbitor_number_object_number_object:
TLbitor_function_string:
TLbitor_boolean_object_builtin:
TLbitor_function_string_object:
TLbitor_simple_object_array:
TLbitor_array_boolean_object:
TLbitor_array_iterator:
TLbitor_regexp_regexp:
TLbitor_string_string_object:
TLbitor_string_object_array:
TLbitor_simple_object_simple_object:
TLbitor_special_number_object:
TLbitor_string_array:
TLbitor_array_array:
TLbitor_simple_object_regexp:
TLbitor_boolean_object_regexp:
TLbitor_builtin_builtin:
TLbitor_array_regexp:
TLbitor_iterator_special:
TLbitor_boolean_object_array:
TLbitor_boolean_object_number_object:
TLbitor_iterator_builtin:
TLbitor_special_iterator:
TLbitor_string_object_simple_object:
TLbitor_function_simple_object:
TLbitor_special_simple_object:
TLbitor_array_function:
TLbitor_boolean_object_boolean_object:
TLbitor_simple_object_iterator:
TLbitor_string_object_builtin:
TLbitor_function_special:
TLbitor_string_object_boolean_object:
TLbitor_boolean_object_simple_object:
TLbitor_special_boolean_object:
TLbitor_string_object_iterator:
TLbitor_iterator_string:
TLbitor_function_function:
TLbitor_string_object_special:
TLbitor_special_string_object:
TLbitor_number_object_boolean_object:
TLbitor_number_object_simple_object:
TLbitor_special_builtin:
TLbitor_builtin_regexp:
TLbitor_string_regexp:
TLbitor_builtin_number_object:
{ // normal_array normal_special vmgen.newsynth.DecisionDiagram$Leaf@16

  v1 = to_number(context, v1);
  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
case TV_FLONUM:
case TV_FIXNUM:
default:
Lbitor3:
TLbitor_flonum_builtin:
TLbitor_fixnum_string_object:
TLbitor_fixnum_builtin:
TLbitor_fixnum_simple_object:
TLbitor_flonum_array:
TLbitor_flonum_function:
TLbitor_fixnum_number_object:
TLbitor_flonum_string:
TLbitor_flonum_special:
TLbitor_flonum_string_object:
TLbitor_fixnum_regexp:
TLbitor_fixnum_special:
TLbitor_fixnum_iterator:
TLbitor_fixnum_function:
TLbitor_flonum_iterator:
TLbitor_fixnum_string:
TLbitor_flonum_number_object:
TLbitor_fixnum_boolean_object:
TLbitor_flonum_regexp:
TLbitor_fixnum_array:
TLbitor_flonum_simple_object:
TLbitor_flonum_boolean_object:
{ // normal_flonum normal_special vmgen.newsynth.DecisionDiagram$Leaf@54

  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@5f3
break;
case TV_FLONUM:
Lbitor4:
TLbitor_any_flonum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@e6b(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lbitor5:
TLbitor_boolean_object_flonum:
TLbitor_iterator_flonum:
TLbitor_iterator_fixnum:
TLbitor_string_fixnum:
TLbitor_simple_object_flonum:
TLbitor_string_object_flonum:
TLbitor_array_flonum:
TLbitor_builtin_fixnum:
TLbitor_builtin_flonum:
TLbitor_function_flonum:
TLbitor_number_object_flonum:
TLbitor_number_object_fixnum:
TLbitor_regexp_fixnum:
TLbitor_function_fixnum:
TLbitor_regexp_flonum:
TLbitor_special_flonum:
TLbitor_special_fixnum:
TLbitor_boolean_object_fixnum:
TLbitor_string_flonum:
TLbitor_string_object_fixnum:
TLbitor_simple_object_fixnum:
TLbitor_array_fixnum:
{ // normal_builtin normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@f3

  v1 = to_number(context, v1);
  goto USELABEL(HEAD);
}
break;
case TV_FIXNUM:
default:
Lbitor6:
TLbitor_fixnum_flonum:
{ // normal_fixnum normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@840

  int32_t x1 = (int32_t) fixnum_to_cint(v1);
  int32_t x2 = (int32_t) flonum_to_cint(v2);
  dst = int32_to_number(context, x1 | x2);
}
break;
case TV_FLONUM:
Lbitor7:
TLbitor_flonum_flonum:
{ // normal_flonum normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@a53

  int32_t x1 = (int32_t) flonum_to_cint(v1);
  int32_t x2 = (int32_t) flonum_to_cint(v2);
  dst = int32_to_number(context, x1 | x2);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@e6b
break;
case TV_FIXNUM:
default:
Lbitor8:
TLbitor_any_fixnum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@1f02(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
goto Lbitor5;
break;
case TV_FLONUM:
Lbitor9:
TLbitor_flonum_fixnum:
{ // normal_flonum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@4ae

  int32_t x1 = (int32_t) flonum_to_cint(v1);
  int32_t x2 = (int32_t) fixnum_to_cint(v2);
  dst = int32_to_number(context, x1 | x2);
}
break;
case TV_FIXNUM:
default:
Lbitor10:
TLbitor_fixnum_fixnum:
{ // normal_fixnum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@947

  dst = v1 | v2;
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@1f02
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@550
#undef dst

