#define dst regbase[r0]

DEFLABEL(HEAD):
INSN_COUNT2(sub,v1,v2);Lsub0:
TLsub_special_any:
TLsub_flonum_any:
TLsub_string_object_any:
TLsub_function_any:
TLsub_number_object_any:
TLsub_any_any:
TLsub_boolean_object_any:
TLsub_simple_object_any:
TLsub_array_any:
TLsub_regexp_any:
TLsub_fixnum_any:
TLsub_string_any:
TLsub_builtin_any:
TLsub_iterator_any:
switch((unsigned int) get_ptag(v2).v){ // vmgen.newsynth.DecisionDiagram$PTNode@550(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lsub1:
TLsub_any_iterator:
TLsub_any_string_object:
TLsub_any_string:
TLsub_any_array:
TLsub_any_builtin:
TLsub_any_boolean_object:
TLsub_any_number_object:
TLsub_any_special:
TLsub_any_simple_object:
TLsub_any_function:
TLsub_any_regexp:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@17c(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lsub2:
TLsub_simple_object_array:
TLsub_iterator_boolean_object:
TLsub_string_object_array:
TLsub_number_object_simple_object:
TLsub_array_regexp:
TLsub_iterator_number_object:
TLsub_special_string:
TLsub_boolean_object_function:
TLsub_builtin_number_object:
TLsub_number_object_regexp:
TLsub_boolean_object_boolean_object:
TLsub_string_object_boolean_object:
TLsub_string_builtin:
TLsub_boolean_object_array:
TLsub_function_array:
TLsub_number_object_array:
TLsub_array_builtin:
TLsub_iterator_string_object:
TLsub_string_special:
TLsub_array_function:
TLsub_regexp_function:
TLsub_builtin_array:
TLsub_regexp_boolean_object:
TLsub_boolean_object_special:
TLsub_builtin_simple_object:
TLsub_string_object_regexp:
TLsub_string_object_iterator:
TLsub_function_number_object:
TLsub_regexp_simple_object:
TLsub_array_number_object:
TLsub_string_number_object:
TLsub_function_function:
TLsub_boolean_object_string:
TLsub_special_array:
TLsub_regexp_regexp:
TLsub_iterator_simple_object:
TLsub_number_object_builtin:
TLsub_function_special:
TLsub_boolean_object_simple_object:
TLsub_builtin_string_object:
TLsub_iterator_builtin:
TLsub_array_simple_object:
TLsub_function_string_object:
TLsub_string_boolean_object:
TLsub_array_iterator:
TLsub_string_object_string_object:
TLsub_simple_object_special:
TLsub_simple_object_function:
TLsub_function_regexp:
TLsub_simple_object_iterator:
TLsub_string_object_string:
TLsub_builtin_string:
TLsub_function_boolean_object:
TLsub_regexp_array:
TLsub_simple_object_builtin:
TLsub_string_regexp:
TLsub_function_builtin:
TLsub_function_simple_object:
TLsub_number_object_string_object:
TLsub_simple_object_boolean_object:
TLsub_simple_object_number_object:
TLsub_boolean_object_string_object:
TLsub_string_function:
TLsub_simple_object_simple_object:
TLsub_number_object_special:
TLsub_regexp_number_object:
TLsub_array_string_object:
TLsub_iterator_array:
TLsub_regexp_iterator:
TLsub_special_special:
TLsub_special_regexp:
TLsub_simple_object_string_object:
TLsub_array_string:
TLsub_regexp_builtin:
TLsub_string_object_function:
TLsub_number_object_string:
TLsub_string_iterator:
TLsub_iterator_function:
TLsub_boolean_object_number_object:
TLsub_number_object_function:
TLsub_string_string:
TLsub_special_number_object:
TLsub_boolean_object_iterator:
TLsub_builtin_iterator:
TLsub_simple_object_regexp:
TLsub_regexp_special:
TLsub_number_object_boolean_object:
TLsub_iterator_special:
TLsub_special_boolean_object:
TLsub_special_builtin:
TLsub_string_object_simple_object:
TLsub_iterator_regexp:
TLsub_function_string:
TLsub_regexp_string:
TLsub_boolean_object_regexp:
TLsub_string_object_number_object:
TLsub_number_object_number_object:
TLsub_array_array:
TLsub_iterator_string:
TLsub_number_object_iterator:
TLsub_regexp_string_object:
TLsub_special_iterator:
TLsub_string_object_special:
TLsub_special_string_object:
TLsub_boolean_object_builtin:
TLsub_string_simple_object:
TLsub_string_array:
TLsub_simple_object_string:
TLsub_builtin_regexp:
TLsub_iterator_iterator:
TLsub_special_function:
TLsub_string_object_builtin:
TLsub_builtin_builtin:
TLsub_array_boolean_object:
TLsub_function_iterator:
TLsub_builtin_boolean_object:
TLsub_builtin_function:
TLsub_builtin_special:
TLsub_array_special:
TLsub_special_simple_object:
TLsub_string_string_object:
{ // normal_builtin normal_regexp vmgen.newsynth.DecisionDiagram$Leaf@16

  v1 = to_number(context, v1);
  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
case TV_FLONUM:
case TV_FIXNUM:
default:
Lsub3:
TLsub_fixnum_regexp:
TLsub_fixnum_number_object:
TLsub_flonum_string:
TLsub_fixnum_string:
TLsub_fixnum_function:
TLsub_flonum_builtin:
TLsub_flonum_array:
TLsub_fixnum_boolean_object:
TLsub_fixnum_builtin:
TLsub_fixnum_string_object:
TLsub_fixnum_simple_object:
TLsub_fixnum_special:
TLsub_flonum_regexp:
TLsub_flonum_number_object:
TLsub_fixnum_array:
TLsub_fixnum_iterator:
TLsub_flonum_boolean_object:
TLsub_flonum_simple_object:
TLsub_flonum_function:
TLsub_flonum_special:
TLsub_flonum_iterator:
TLsub_flonum_string_object:
{ // normal_fixnum normal_string vmgen.newsynth.DecisionDiagram$Leaf@222

  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@17c
break;
case TV_FLONUM:
Lsub4:
TLsub_any_flonum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@173c(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lsub5:
TLsub_iterator_flonum:
TLsub_regexp_fixnum:
TLsub_special_fixnum:
TLsub_builtin_fixnum:
TLsub_number_object_flonum:
TLsub_function_fixnum:
TLsub_iterator_fixnum:
TLsub_boolean_object_flonum:
TLsub_string_object_flonum:
TLsub_string_flonum:
TLsub_simple_object_flonum:
TLsub_number_object_fixnum:
TLsub_array_flonum:
TLsub_builtin_flonum:
TLsub_string_fixnum:
TLsub_special_flonum:
TLsub_string_object_fixnum:
TLsub_regexp_flonum:
TLsub_simple_object_fixnum:
TLsub_array_fixnum:
TLsub_function_flonum:
TLsub_boolean_object_fixnum:
{ // normal_regexp normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@df

  v1 = to_number(context, v1);
  goto USELABEL(HEAD);
}
break;
case TV_FLONUM:
case TV_FIXNUM:
default:
Lsub6:
TLsub_fixnum_flonum:
TLsub_flonum_fixnum:
TLsub_flonum_flonum:
{ // normal_flonum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@13e8

  dst = double_to_number(context, to_double(context, v1) - to_double(context, v2));
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@173c
break;
case TV_FIXNUM:
default:
Lsub7:
TLsub_any_fixnum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@220f(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
goto Lsub5;
break;
case TV_FIXNUM:
default:
Lsub8:
TLsub_fixnum_fixnum:
{ // normal_fixnum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@a53

  cint s = fixnum_to_cint(v1) - fixnum_to_cint(v2);
  dst = cint_to_number(context, s);
}
break;
case TV_FLONUM:
goto Lsub6;
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@220f
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@550
#undef dst

