#define dst regbase[r0]

DEFLABEL(HEAD):
INSN_COUNT2(mod,v1,v2);Lmod0:
TLmod_flonum_any:
TLmod_builtin_any:
TLmod_iterator_any:
TLmod_function_any:
TLmod_string_object_any:
TLmod_regexp_any:
TLmod_array_any:
TLmod_any_any:
TLmod_simple_object_any:
TLmod_string_any:
TLmod_boolean_object_any:
TLmod_number_object_any:
TLmod_special_any:
TLmod_fixnum_any:
switch((unsigned int) get_ptag(v2).v){ // vmgen.newsynth.DecisionDiagram$PTNode@550(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lmod1:
TLmod_any_special:
TLmod_any_string:
TLmod_any_iterator:
TLmod_any_string_object:
TLmod_any_builtin:
TLmod_any_simple_object:
TLmod_any_array:
TLmod_any_function:
TLmod_any_number_object:
TLmod_any_regexp:
TLmod_any_boolean_object:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@17c(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lmod2:
TLmod_simple_object_number_object:
TLmod_builtin_builtin:
TLmod_string_array:
TLmod_number_object_string_object:
TLmod_regexp_string_object:
TLmod_boolean_object_simple_object:
TLmod_simple_object_regexp:
TLmod_array_string_object:
TLmod_simple_object_function:
TLmod_number_object_iterator:
TLmod_boolean_object_builtin:
TLmod_string_function:
TLmod_builtin_number_object:
TLmod_string_object_simple_object:
TLmod_simple_object_array:
TLmod_builtin_simple_object:
TLmod_regexp_iterator:
TLmod_builtin_string:
TLmod_number_object_array:
TLmod_string_string_object:
TLmod_array_string:
TLmod_special_array:
TLmod_regexp_boolean_object:
TLmod_special_iterator:
TLmod_number_object_string:
TLmod_iterator_array:
TLmod_builtin_special:
TLmod_function_simple_object:
TLmod_boolean_object_string:
TLmod_string_object_array:
TLmod_function_boolean_object:
TLmod_builtin_iterator:
TLmod_iterator_function:
TLmod_boolean_object_iterator:
TLmod_boolean_object_regexp:
TLmod_regexp_function:
TLmod_special_regexp:
TLmod_simple_object_simple_object:
TLmod_number_object_regexp:
TLmod_string_object_function:
TLmod_string_object_boolean_object:
TLmod_string_builtin:
TLmod_iterator_number_object:
TLmod_function_builtin:
TLmod_regexp_string:
TLmod_string_object_special:
TLmod_iterator_string_object:
TLmod_special_string_object:
TLmod_simple_object_string_object:
TLmod_string_object_builtin:
TLmod_array_special:
TLmod_array_array:
TLmod_array_function:
TLmod_boolean_object_number_object:
TLmod_special_special:
TLmod_function_function:
TLmod_number_object_boolean_object:
TLmod_builtin_array:
TLmod_string_special:
TLmod_array_builtin:
TLmod_iterator_simple_object:
TLmod_special_builtin:
TLmod_iterator_boolean_object:
TLmod_special_simple_object:
TLmod_function_special:
TLmod_iterator_string:
TLmod_string_simple_object:
TLmod_iterator_special:
TLmod_special_function:
TLmod_iterator_regexp:
TLmod_regexp_array:
TLmod_iterator_builtin:
TLmod_special_number_object:
TLmod_array_boolean_object:
TLmod_array_iterator:
TLmod_function_string_object:
TLmod_builtin_regexp:
TLmod_function_string:
TLmod_number_object_function:
TLmod_simple_object_boolean_object:
TLmod_builtin_boolean_object:
TLmod_boolean_object_function:
TLmod_builtin_function:
TLmod_string_string:
TLmod_number_object_simple_object:
TLmod_boolean_object_string_object:
TLmod_boolean_object_boolean_object:
TLmod_simple_object_string:
TLmod_string_object_string:
TLmod_string_object_string_object:
TLmod_array_regexp:
TLmod_boolean_object_array:
TLmod_function_iterator:
TLmod_builtin_string_object:
TLmod_function_number_object:
TLmod_function_array:
TLmod_regexp_builtin:
TLmod_string_boolean_object:
TLmod_string_number_object:
TLmod_number_object_builtin:
TLmod_special_string:
TLmod_regexp_regexp:
TLmod_simple_object_iterator:
TLmod_string_object_regexp:
TLmod_regexp_number_object:
TLmod_iterator_iterator:
TLmod_string_regexp:
TLmod_simple_object_special:
TLmod_array_number_object:
TLmod_string_iterator:
TLmod_number_object_special:
TLmod_function_regexp:
TLmod_regexp_special:
TLmod_regexp_simple_object:
TLmod_string_object_iterator:
TLmod_special_boolean_object:
TLmod_array_simple_object:
TLmod_string_object_number_object:
TLmod_number_object_number_object:
TLmod_boolean_object_special:
TLmod_simple_object_builtin:
{ // normal_builtin normal_regexp vmgen.newsynth.DecisionDiagram$Leaf@16

  v1 = to_number(context, v1);
  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
case TV_FLONUM:
case TV_FIXNUM:
default:
Lmod3:
TLmod_flonum_iterator:
TLmod_fixnum_function:
TLmod_flonum_string_object:
TLmod_fixnum_iterator:
TLmod_flonum_array:
TLmod_fixnum_builtin:
TLmod_fixnum_array:
TLmod_flonum_number_object:
TLmod_flonum_simple_object:
TLmod_fixnum_regexp:
TLmod_fixnum_string_object:
TLmod_fixnum_string:
TLmod_fixnum_special:
TLmod_flonum_string:
TLmod_fixnum_simple_object:
TLmod_flonum_function:
TLmod_fixnum_number_object:
TLmod_flonum_boolean_object:
TLmod_flonum_builtin:
TLmod_flonum_special:
TLmod_fixnum_boolean_object:
TLmod_flonum_regexp:
{ // normal_fixnum normal_string vmgen.newsynth.DecisionDiagram$Leaf@222

  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@17c
break;
case TV_FLONUM:
Lmod4:
TLmod_any_flonum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@173c(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
Lmod5:
TLmod_boolean_object_fixnum:
TLmod_string_flonum:
TLmod_array_fixnum:
TLmod_function_flonum:
TLmod_string_fixnum:
TLmod_special_fixnum:
TLmod_function_fixnum:
TLmod_regexp_flonum:
TLmod_iterator_flonum:
TLmod_boolean_object_flonum:
TLmod_number_object_flonum:
TLmod_builtin_fixnum:
TLmod_simple_object_fixnum:
TLmod_string_object_fixnum:
TLmod_regexp_fixnum:
TLmod_string_object_flonum:
TLmod_iterator_fixnum:
TLmod_special_flonum:
TLmod_number_object_fixnum:
TLmod_simple_object_flonum:
TLmod_array_flonum:
TLmod_builtin_flonum:
{ // normal_regexp normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@df

  v1 = to_number(context, v1);
  goto USELABEL(HEAD);
}
break;
case TV_FLONUM:
case TV_FIXNUM:
default:
Lmod6:
TLmod_fixnum_flonum:
TLmod_flonum_flonum:
TLmod_flonum_fixnum:
{ // normal_flonum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@13e8

  double x1 = to_double(context, v1);
  double x2 = to_double(context, v2);
  double d;
  if (isinf(x1) || x2 == 0.0f)
    dst = gconsts.g_flonum_nan;
  else {
    d = x1 / x2;
    d = d >= 0? floor(d) : ceil(d);
    d = x1 - (d * x2);
    dst = double_to_number(context, d);
  }
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@173c
break;
case TV_FIXNUM:
default:
Lmod7:
TLmod_any_fixnum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@220f(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
case TV_GENERIC:
goto Lmod5;
break;
case TV_FIXNUM:
default:
Lmod8:
TLmod_fixnum_fixnum:
{ // normal_fixnum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@a53

  if (v2 == FIXNUM_ZERO)
    dst = gconsts.g_flonum_nan;
  else {
    cint s = fixnum_to_cint(v1) % fixnum_to_cint(v2);
    dst = cint_to_number(context, s);
  }
}
break;
case TV_FLONUM:
goto Lmod6;
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@220f
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@550
#undef dst

