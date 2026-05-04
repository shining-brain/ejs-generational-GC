#define dst regbase[r0]

DEFLABEL(HEAD):
INSN_COUNT2(lessthanequal,v1,v2);Llessthanequal0:
TLlessthanequal_number_object_any:
TLlessthanequal_boolean_object_any:
TLlessthanequal_fixnum_any:
TLlessthanequal_string_any:
TLlessthanequal_builtin_any:
TLlessthanequal_special_any:
TLlessthanequal_simple_object_any:
TLlessthanequal_iterator_any:
TLlessthanequal_array_any:
TLlessthanequal_regexp_any:
TLlessthanequal_function_any:
TLlessthanequal_any_any:
TLlessthanequal_flonum_any:
TLlessthanequal_string_object_any:
switch((unsigned int) get_ptag(v2).v){ // vmgen.newsynth.DecisionDiagram$PTNode@550(5)
case TV_GENERIC:
Llessthanequal1:
TLlessthanequal_any_string_object:
TLlessthanequal_any_function:
TLlessthanequal_any_simple_object:
TLlessthanequal_any_iterator:
TLlessthanequal_any_builtin:
TLlessthanequal_any_array:
TLlessthanequal_any_boolean_object:
TLlessthanequal_any_number_object:
TLlessthanequal_any_regexp:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@54(2)
case TV_GENERIC:
Llessthanequal2:
TLlessthanequal_number_object_string_object:
TLlessthanequal_string_object_string_object:
TLlessthanequal_string_object_function:
TLlessthanequal_iterator_boolean_object:
TLlessthanequal_iterator_function:
TLlessthanequal_iterator_builtin:
TLlessthanequal_simple_object_number_object:
TLlessthanequal_iterator_special:
TLlessthanequal_regexp_function:
TLlessthanequal_array_string_object:
TLlessthanequal_fixnum_iterator:
TLlessthanequal_string_object_iterator:
TLlessthanequal_simple_object_array:
TLlessthanequal_number_object_array:
TLlessthanequal_builtin_simple_object:
TLlessthanequal_boolean_object_number_object:
TLlessthanequal_function_number_object:
TLlessthanequal_function_function:
TLlessthanequal_iterator_number_object:
TLlessthanequal_string_object_array:
TLlessthanequal_function_iterator:
TLlessthanequal_string_object_number_object:
TLlessthanequal_builtin_function:
TLlessthanequal_number_object_function:
TLlessthanequal_iterator_iterator:
TLlessthanequal_function_regexp:
TLlessthanequal_number_object_builtin:
TLlessthanequal_regexp_iterator:
TLlessthanequal_boolean_object_simple_object:
TLlessthanequal_array_iterator:
TLlessthanequal_simple_object_function:
TLlessthanequal_string_object_boolean_object:
TLlessthanequal_builtin_string_object:
TLlessthanequal_array_boolean_object:
TLlessthanequal_function_array:
TLlessthanequal_regexp_builtin:
TLlessthanequal_number_object_boolean_object:
TLlessthanequal_special_iterator:
TLlessthanequal_array_number_object:
TLlessthanequal_boolean_object_iterator:
TLlessthanequal_regexp_number_object:
TLlessthanequal_builtin_builtin:
TLlessthanequal_simple_object_boolean_object:
TLlessthanequal_iterator_simple_object:
TLlessthanequal_builtin_iterator:
TLlessthanequal_boolean_object_regexp:
TLlessthanequal_regexp_boolean_object:
TLlessthanequal_function_string_object:
TLlessthanequal_number_object_iterator:
TLlessthanequal_regexp_simple_object:
TLlessthanequal_boolean_object_builtin:
TLlessthanequal_simple_object_builtin:
TLlessthanequal_boolean_object_string_object:
TLlessthanequal_iterator_array:
TLlessthanequal_string_object_builtin:
TLlessthanequal_boolean_object_array:
TLlessthanequal_simple_object_simple_object:
TLlessthanequal_iterator_string:
TLlessthanequal_iterator_fixnum:
TLlessthanequal_array_array:
TLlessthanequal_regexp_array:
TLlessthanequal_array_regexp:
TLlessthanequal_builtin_array:
TLlessthanequal_array_simple_object:
TLlessthanequal_iterator_string_object:
TLlessthanequal_array_builtin:
TLlessthanequal_number_object_simple_object:
TLlessthanequal_simple_object_iterator:
TLlessthanequal_function_boolean_object:
TLlessthanequal_number_object_number_object:
TLlessthanequal_iterator_flonum:
TLlessthanequal_function_builtin:
TLlessthanequal_flonum_iterator:
TLlessthanequal_iterator_regexp:
TLlessthanequal_regexp_string_object:
TLlessthanequal_function_simple_object:
TLlessthanequal_simple_object_string_object:
TLlessthanequal_builtin_regexp:
TLlessthanequal_number_object_regexp:
TLlessthanequal_boolean_object_boolean_object:
TLlessthanequal_builtin_number_object:
TLlessthanequal_regexp_regexp:
TLlessthanequal_string_iterator:
TLlessthanequal_string_object_simple_object:
TLlessthanequal_builtin_boolean_object:
TLlessthanequal_simple_object_regexp:
TLlessthanequal_string_object_regexp:
TLlessthanequal_array_function:
TLlessthanequal_boolean_object_function:
{ // normal_array normal_iterator vmgen.newsynth.DecisionDiagram$Leaf@69

  v1 = object_to_primitive(context, v1, HINT_NUMBER);
  v2 = object_to_primitive(context, v2, HINT_NUMBER);
  goto USELABEL(HEAD);
}
break;
case TV_STRING:
case 3:
case 2:
case 1:
case TV_FLONUM:
case TV_SPECIAL:
case TV_FIXNUM:
default:
Llessthanequal3:switch((unsigned int) get_htag(v2).v){ // vmgen.newsynth.DecisionDiagram$HTNode@7cf(2)
case HTAGV_ITERATOR:
goto Llessthanequal2;
break;
case HTAGV_SIMPLE_OBJECT:
case 5:
case 4:
case 3:
case 2:
case 1:
case 0:
case HTAGV_ARRAY:
case HTAGV_FUNCTION:
case HTAGV_BUILTIN:
case HTAGV_REGEXP:
case HTAGV_BOXED_STRING:
case HTAGV_BOXED_NUMBER:
case HTAGV_BOXED_BOOLEAN:
default:
Llessthanequal4:
TLlessthanequal_string_function:
TLlessthanequal_flonum_builtin:
TLlessthanequal_flonum_function:
TLlessthanequal_special_string_object:
TLlessthanequal_fixnum_string_object:
TLlessthanequal_flonum_regexp:
TLlessthanequal_string_simple_object:
TLlessthanequal_fixnum_regexp:
TLlessthanequal_string_boolean_object:
TLlessthanequal_flonum_simple_object:
TLlessthanequal_special_boolean_object:
TLlessthanequal_string_array:
TLlessthanequal_fixnum_array:
TLlessthanequal_string_number_object:
TLlessthanequal_fixnum_function:
TLlessthanequal_fixnum_simple_object:
TLlessthanequal_fixnum_boolean_object:
TLlessthanequal_flonum_array:
TLlessthanequal_special_builtin:
TLlessthanequal_special_simple_object:
TLlessthanequal_flonum_boolean_object:
TLlessthanequal_fixnum_builtin:
TLlessthanequal_fixnum_number_object:
TLlessthanequal_string_regexp:
TLlessthanequal_special_number_object:
TLlessthanequal_special_array:
TLlessthanequal_string_string_object:
TLlessthanequal_flonum_string_object:
TLlessthanequal_flonum_number_object:
TLlessthanequal_special_function:
TLlessthanequal_special_regexp:
TLlessthanequal_string_builtin:
{ // normal_flonum normal_array vmgen.newsynth.DecisionDiagram$Leaf@222

  v2 = object_to_primitive(context, v2, HINT_NUMBER);
  goto USELABEL(HEAD);
}
break;
}// vmgen.newsynth.DecisionDiagram$HTNode@7cf
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@54
break;
case TV_SPECIAL:
Llessthanequal5:
TLlessthanequal_any_special:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@91b(3)
case TV_FLONUM:
case TV_FIXNUM:
default:
Llessthanequal6:
TLlessthanequal_flonum_special:
TLlessthanequal_fixnum_string:
TLlessthanequal_flonum_string:
TLlessthanequal_fixnum_special:
{ // normal_fixnum normal_special vmgen.newsynth.DecisionDiagram$Leaf@2be

  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
Llessthanequal7:
TLlessthanequal_special_special:
TLlessthanequal_string_special:
TLlessthanequal_special_string:
{ // normal_special normal_special vmgen.newsynth.DecisionDiagram$Leaf@50e

  v1 = to_number(context, v1);
  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
case TV_GENERIC:
Llessthanequal8:switch((unsigned int) get_htag(v1).v){ // vmgen.newsynth.DecisionDiagram$HTNode@1f54(2)
case HTAGV_ITERATOR:
goto Llessthanequal2;
break;
case HTAGV_SIMPLE_OBJECT:
case 5:
case 4:
case 3:
case 2:
case 1:
case 0:
case HTAGV_ARRAY:
case HTAGV_FUNCTION:
case HTAGV_BUILTIN:
case HTAGV_REGEXP:
case HTAGV_BOXED_STRING:
case HTAGV_BOXED_NUMBER:
case HTAGV_BOXED_BOOLEAN:
default:
Llessthanequal9:
TLlessthanequal_regexp_flonum:
TLlessthanequal_simple_object_string:
TLlessthanequal_string_object_string:
TLlessthanequal_regexp_special:
TLlessthanequal_simple_object_fixnum:
TLlessthanequal_regexp_fixnum:
TLlessthanequal_string_object_fixnum:
TLlessthanequal_regexp_string:
TLlessthanequal_boolean_object_fixnum:
TLlessthanequal_number_object_fixnum:
TLlessthanequal_number_object_special:
TLlessthanequal_builtin_flonum:
TLlessthanequal_number_object_flonum:
TLlessthanequal_builtin_fixnum:
TLlessthanequal_boolean_object_string:
TLlessthanequal_builtin_special:
TLlessthanequal_number_object_string:
TLlessthanequal_simple_object_flonum:
TLlessthanequal_array_string:
TLlessthanequal_string_object_flonum:
TLlessthanequal_builtin_string:
TLlessthanequal_function_string:
TLlessthanequal_function_special:
TLlessthanequal_array_fixnum:
TLlessthanequal_boolean_object_special:
TLlessthanequal_array_flonum:
TLlessthanequal_array_special:
TLlessthanequal_boolean_object_flonum:
TLlessthanequal_function_flonum:
TLlessthanequal_function_fixnum:
TLlessthanequal_simple_object_special:
TLlessthanequal_string_object_special:
{ // normal_number_object normal_special vmgen.newsynth.DecisionDiagram$Leaf@285

  v1 = object_to_primitive(context, v1, HINT_NUMBER);
  goto USELABEL(HEAD);
}
break;
}// vmgen.newsynth.DecisionDiagram$HTNode@1f54
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@91b
break;
case TV_FLONUM:
Llessthanequal10:
TLlessthanequal_any_flonum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@173c(3)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
Llessthanequal11:
TLlessthanequal_special_flonum:
TLlessthanequal_string_flonum:
TLlessthanequal_string_fixnum:
TLlessthanequal_special_fixnum:
{ // normal_special normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@635

  v1 = to_number(context, v1);
  goto USELABEL(HEAD);
}
break;
case TV_FLONUM:
case TV_FIXNUM:
default:
Llessthanequal12:
TLlessthanequal_fixnum_flonum:
TLlessthanequal_flonum_fixnum:
TLlessthanequal_flonum_flonum:
{ // normal_flonum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@a75

  double x1 = to_double(context, v1);
  double x2 = to_double(context, v2);
  dst = true_false(x1 <= x2);
}
break;
case TV_GENERIC:
goto Llessthanequal8;
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@173c
break;
case TV_STRING:
case 3:
case 2:
case 1:
Llessthanequal13:
TLlessthanequal_any_string:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@1d31(4)
case TV_FLONUM:
case TV_FIXNUM:
default:
goto Llessthanequal6;
break;
case TV_SPECIAL:
goto Llessthanequal7;
break;
case TV_GENERIC:
goto Llessthanequal8;
break;
case TV_STRING:
case 3:
case 2:
case 1:
Llessthanequal14:
TLlessthanequal_string_string:
{ // normal_string normal_string vmgen.newsynth.DecisionDiagram$Leaf@228c

  dst = true_false(strcmp(string_to_cstr(v1), string_to_cstr(v2)) <= 0);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@1d31
break;
case TV_FIXNUM:
default:
Llessthanequal15:
TLlessthanequal_any_fixnum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@220f(4)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
goto Llessthanequal11;
break;
case TV_FIXNUM:
default:
Llessthanequal16:
TLlessthanequal_fixnum_fixnum:
{ // normal_fixnum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@a53

  dst = true_false(((intjsv_t) v1) <= ((intjsv_t) v2));
}
break;
case TV_FLONUM:
goto Llessthanequal12;
break;
case TV_GENERIC:
goto Llessthanequal8;
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@220f
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@550
#undef dst

